//! The HEVC encoder (VideoToolbox, in hardware). Takes BGRA frames, produces Annex B NAL units:
//! the parameter sets (VPS/SPS/PPS) apart, and one buffer per frame.

use anyhow::{bail, Result};
use objc2_core_foundation::{CFArray, CFBoolean, CFDictionary, CFNumber, CFRetained, CFString, CFType};
use objc2_core_media::{
    kCMSampleAttachmentKey_NotSync, kCMVideoCodecType_HEVC, CMFormatDescription,
    CMSampleBuffer, CMTime, CMVideoFormatDescriptionGetHEVCParameterSetAtIndex,
};
use objc2_core_video::{
    kCVImageBufferColorPrimaries_ITU_R_709_2, kCVImageBufferTransferFunction_ITU_R_709_2,
    kCVImageBufferYCbCrMatrix_ITU_R_709_2, kCVPixelBufferHeightKey, kCVPixelBufferIOSurfacePropertiesKey, kCVPixelBufferPixelFormatTypeKey,
    kCVPixelBufferWidthKey, kCVPixelFormatType_32BGRA, CVPixelBuffer, CVPixelBufferGetBaseAddress,
    CVPixelBufferGetBytesPerRow, CVPixelBufferLockBaseAddress, CVPixelBufferLockFlags, CVPixelBufferPool,
    CVPixelBufferUnlockBaseAddress,
};
use objc2_video_toolbox::{
    kVTCompressionPropertyKey_AllowFrameReordering, kVTCompressionPropertyKey_AverageBitRate, kVTCompressionPropertyKey_DataRateLimits,
    kVTCompressionPropertyKey_ColorPrimaries, kVTCompressionPropertyKey_TransferFunction,
    kVTCompressionPropertyKey_YCbCrMatrix,
    kVTCompressionPropertyKey_ExpectedFrameRate, kVTCompressionPropertyKey_MaxFrameDelayCount,
    kVTCompressionPropertyKey_MaxKeyFrameInterval, kVTCompressionPropertyKey_PrioritizeEncodingSpeedOverQuality,
    kVTCompressionPropertyKey_ProfileLevel, kVTCompressionPropertyKey_RealTime, kVTEncodeFrameOptionKey_ForceKeyFrame,
    kVTProfileLevel_HEVC_Main_AutoLevel,
    kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder, VTCompressionSession, VTEncodeInfoFlags,
    VTSessionSetProperty,
};
use std::{
    ffi::c_void,
    ptr::{self, NonNull},
    sync::mpsc::{self, Receiver, Sender},
    time::{Duration, Instant},
};

const ANNEX_B_START_CODE: [u8; 4] = [0, 0, 0, 1];

pub struct EncodedFrame {
    /// The tracking timestamp this frame was rendered for.
    pub target_timestamp: Duration,
    pub is_idr: bool,
    /// Annex B NAL units for this frame, parameter sets excluded.
    pub nals: Vec<u8>,
    /// Annex B parameter sets, present when they changed (always on the first
    /// frame and after every IDR).
    pub config: Option<Vec<u8>>,
    pub encode_time: Duration,
}

/// Owned by the output callback through a raw pointer for the session's life.
struct CallbackContext {
    sender: Sender<EncodedFrame>,
}

/// Per frame, handed to VideoToolbox and back through the callback.
struct FrameContext {
    target_timestamp: Duration,
    submitted_at: Instant,
}

pub struct Encoder {
    session: CFRetained<VTCompressionSession>,
    pool: CFRetained<CVPixelBufferPool>,
    callback_context: *mut CallbackContext,
    height: u32,
    frame_index: i64,
    fps: f32,
    current_bitrate_bps: u64,
}

/// The least frame rate the encoder is told to expect.
const LEAST_EXPECTED_RATE: f32 = 120.0;

/// Caps on how far over its average the encoder may go, so its bursts don't overflow the network:
/// half over across a second, double across a quarter of a second (room for a keyframe). Tighter
/// caps made it undershoot the average by an eighth (encoder_benchmark).
fn set_rate_limits(session: &VTCompressionSession, bitrate_bps: u64) {
    let bytes_per_second = bitrate_bps as f64 / 8.0;
    let limits = [(bytes_per_second * 1.5, 1.0), (bytes_per_second * 2.0 * 0.25, 0.25)];
    let numbers: Vec<CFRetained<CFNumber>> = limits.iter().flat_map(|&(bytes, seconds)| [CFNumber::new_f64(bytes), CFNumber::new_f64(seconds)]).collect();
    let refs: Vec<&CFType> = numbers.iter().map(|n| &**n as &CFType).collect();
    let array: CFRetained<CFArray<CFType>> = CFArray::from_objects(&refs);
    if let Err(e) = set_property(session, unsafe { kVTCompressionPropertyKey_DataRateLimits }, &array) {
        log::warn!("{e}");
    }
}

fn set_property(session: &VTCompressionSession, key: &CFString, value: &CFType) -> Result<()> {
    let status = unsafe { VTSessionSetProperty(session, key, Some(value)) };
    if status != 0 {
        bail!("VTSessionSetProperty({key}) failed: {status}");
    }
    Ok(())
}

impl Encoder {
    pub fn new(width: u32, height: u32, fps: f32, bitrate_bps: u64) -> Result<(Self, Receiver<EncodedFrame>)> {
        Self::with_input(width, height, fps, bitrate_bps, kCVPixelFormatType_32BGRA)
    }

    /// The same, taking frames in another Core Video pixel format (`input`).
    pub fn with_input(width: u32, height: u32, fps: f32, bitrate_bps: u64, input: u32) -> Result<(Self, Receiver<EncodedFrame>)> {
        let (sender, receiver) = mpsc::channel();
        let callback_context = Box::into_raw(Box::new(CallbackContext { sender }));

        // Deliberately not kVTVideoEncoderSpecification_EnableLowLatencyRateControl:
        // on an M4 Max that mode capped the encoder at about 350 Mpx/s (41 fps
        // at 4096x2208) with 115 ms of queueing. Without it the same stream
        // encodes at 72 fps in 13 ms (and 3328x1792 in 13 ms against 74 with it).
        let true_value: &CFType = CFBoolean::new(true);
        let spec_keys: [&CFString; 1] = [unsafe { kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder }];
        let specification: CFRetained<CFDictionary<CFString, CFType>> =
            CFDictionary::from_slices(&spec_keys, &[true_value]);

        let format_number = CFNumber::new_i32(input as i32);
        let empty: CFRetained<CFDictionary<CFString, CFType>> = CFDictionary::from_slices(&[], &[]);
        let attribute_keys: [&CFString; 2] =
            [unsafe { kCVPixelBufferPixelFormatTypeKey }, unsafe { kCVPixelBufferIOSurfacePropertiesKey }];
        let attribute_values: [&CFType; 2] = [&format_number, &empty];
        let source_attributes: CFRetained<CFDictionary<CFString, CFType>> =
            CFDictionary::from_slices(&attribute_keys, &attribute_values);

        let mut session_ptr: *mut VTCompressionSession = ptr::null_mut();
        let status = unsafe {
            VTCompressionSession::create(
                None,
                width as i32,
                height as i32,
                kCMVideoCodecType_HEVC,
                Some(specification.as_opaque()),
                Some(source_attributes.as_opaque()),
                None,
                Some(output_callback),
                callback_context as *mut c_void,
                NonNull::from(&mut session_ptr),
            )
        };
        if status != 0 || session_ptr.is_null() {
            unsafe { drop(Box::from_raw(callback_context)) };
            bail!("VTCompressionSessionCreate failed: {status}");
        }
        let session = unsafe { CFRetained::from_raw(NonNull::new_unchecked(session_ptr)) };

        set_property(&session, unsafe { kVTCompressionPropertyKey_RealTime }, true_value)?;
        set_property(&session, unsafe { kVTCompressionPropertyKey_AllowFrameReordering }, CFBoolean::new(false))?;
        set_property(&session, unsafe { kVTCompressionPropertyKey_ProfileLevel }, unsafe { kVTProfileLevel_HEVC_Main_AutoLevel })?;
        // The encoder runs at the pace it expects, and at 72 it slows to fit: told to expect at least
        // 120 it encodes each frame about a third sooner (encoder_benchmark). Its bitrate follows
        // the frames' timestamps, which keep the real rate.
        let expected = fps.max(LEAST_EXPECTED_RATE);
        set_property(&session, unsafe { kVTCompressionPropertyKey_ExpectedFrameRate }, &CFNumber::new_f64(expected as f64))?;
        // Not supported by the Apple silicon hardware encoder; harmless to skip.
        set_property(&session, unsafe { kVTCompressionPropertyKey_MaxFrameDelayCount }, &CFNumber::new_i32(0)).ok();
        // The headset asks for IDR frames itself; do not insert periodic ones.
        set_property(&session, unsafe { kVTCompressionPropertyKey_MaxKeyFrameInterval }, &CFNumber::new_i32(i32::MAX))?;
        set_property(&session, unsafe { kVTCompressionPropertyKey_AverageBitRate }, &CFNumber::new_i64(bitrate_bps as i64))?;
        set_rate_limits(&session, bitrate_bps);
        // Optional on some hardware; a failure here is not fatal.
        if let Err(e) = set_property(&session, unsafe { kVTCompressionPropertyKey_PrioritizeEncodingSpeedOverQuality }, true_value) {
            log::warn!("{e}");
        }
        // The game renders sRGB; tag the stream as BT.709 so the decoder does
        // not guess.
        for (key, value) in [
            (unsafe { kVTCompressionPropertyKey_ColorPrimaries }, unsafe { kCVImageBufferColorPrimaries_ITU_R_709_2 }),
            (unsafe { kVTCompressionPropertyKey_TransferFunction }, unsafe { kCVImageBufferTransferFunction_ITU_R_709_2 }),
            (unsafe { kVTCompressionPropertyKey_YCbCrMatrix }, unsafe { kCVImageBufferYCbCrMatrix_ITU_R_709_2 }),
        ] {
            if let Err(e) = set_property(&session, key, value) {
                log::warn!("{e}");
            }
        }

        let mut pool_ptr: *mut CVPixelBufferPool = ptr::null_mut();
        let width_number = CFNumber::new_i32(width as i32);
        let height_number = CFNumber::new_i32(height as i32);
        let pool_keys: [&CFString; 4] = [
            unsafe { kCVPixelBufferPixelFormatTypeKey },
            unsafe { kCVPixelBufferWidthKey },
            unsafe { kCVPixelBufferHeightKey },
            unsafe { kCVPixelBufferIOSurfacePropertiesKey },
        ];
        let pool_values: [&CFType; 4] = [&format_number, &width_number, &height_number, &empty];
        let pool_attributes: CFRetained<CFDictionary<CFString, CFType>> =
            CFDictionary::from_slices(&pool_keys, &pool_values);
        let status = unsafe {
            CVPixelBufferPool::create(None, None, Some(pool_attributes.as_opaque()), NonNull::from(&mut pool_ptr))
        };
        if status != 0 || pool_ptr.is_null() {
            bail!("CVPixelBufferPoolCreate failed: {status}");
        }
        let pool = unsafe { CFRetained::from_raw(NonNull::new_unchecked(pool_ptr)) };

        Ok((
            Self {
                session,
                pool,
                callback_context,
                height,
                frame_index: 0,
                fps,
                current_bitrate_bps: bitrate_bps,
            },
            receiver,
        ))
    }

    pub fn set_bitrate(&mut self, bitrate_bps: u64) {
        if bitrate_bps == self.current_bitrate_bps || bitrate_bps == 0 {
            return;
        }
        if set_property(&self.session, unsafe { kVTCompressionPropertyKey_AverageBitRate }, &CFNumber::new_i64(bitrate_bps as i64)).is_ok() {
            set_rate_limits(&self.session, bitrate_bps);
            self.current_bitrate_bps = bitrate_bps;
        }
    }

    /// Borrows a pooled BGRA buffer and lets `fill` write the pixels into it.
    /// `fill` receives the destination bytes and the row stride and returns the
    /// The tracking timestamp to stamp the frame with, or None to skip it.
    pub fn encode_with(
        &mut self,
        force_idr: bool,
        fill: impl FnOnce(&mut [u8], usize) -> Option<Duration>,
    ) -> Result<bool> {
        let height = self.height as usize;
        self.encode_buffer_with(force_idr, |buffer| unsafe {
            let base = CVPixelBufferGetBaseAddress(buffer) as *mut u8;
            let stride = CVPixelBufferGetBytesPerRow(buffer);
            if base.is_null() {
                None
            } else {
                fill(std::slice::from_raw_parts_mut(base, stride * height), stride)
            }
        })
    }

    /// Borrows a pooled buffer, locked, and lets `fill` write it (any planes) and return the
    /// tracking timestamp, or None to skip the frame.
    pub fn encode_buffer_with(
        &mut self,
        force_idr: bool,
        fill: impl FnOnce(&CVPixelBuffer) -> Option<Duration>,
    ) -> Result<bool> {
        let mut buffer_ptr: *mut CVPixelBuffer = ptr::null_mut();
        let status = unsafe { CVPixelBufferPool::create_pixel_buffer(None, &self.pool, NonNull::from(&mut buffer_ptr)) };
        if status != 0 || buffer_ptr.is_null() {
            bail!("CVPixelBufferPoolCreatePixelBuffer failed: {status}");
        }
        let buffer: CFRetained<CVPixelBuffer> = unsafe { CFRetained::from_raw(NonNull::new_unchecked(buffer_ptr)) };

        let filled = unsafe {
            CVPixelBufferLockBaseAddress(&buffer, CVPixelBufferLockFlags::empty());
            let filled = fill(&buffer);
            CVPixelBufferUnlockBaseAddress(&buffer, CVPixelBufferLockFlags::empty());
            filled
        };
        let Some(target_timestamp) = filled else {
            return Ok(false);
        };

        let frame_context = Box::into_raw(Box::new(FrameContext { target_timestamp, submitted_at: Instant::now() }));
        let timescale = 1_000_000;
        let presentation = unsafe {
            CMTime::new((self.frame_index as f64 * 1_000_000.0 / self.fps as f64) as i64, timescale)
        };
        let duration = unsafe { CMTime::new((1_000_000.0 / self.fps as f64) as i64, timescale) };
        self.frame_index += 1;

        let force_key: Option<CFRetained<CFDictionary<CFString, CFType>>> = force_idr.then(|| {
            let keys: [&CFString; 1] = [unsafe { kVTEncodeFrameOptionKey_ForceKeyFrame }];
            let values: [&CFType; 1] = [CFBoolean::new(true)];
            CFDictionary::from_slices(&keys, &values)
        });

        let mut flags = VTEncodeInfoFlags::empty();
        let status = unsafe {
            self.session.encode_frame(
                &buffer,
                presentation,
                duration,
                force_key.as_deref().map(|d| d.as_opaque()),
                frame_context as *mut c_void,
                &mut flags,
            )
        };
        if status != 0 {
            unsafe { drop(Box::from_raw(frame_context)) };
            bail!("VTCompressionSessionEncodeFrame failed: {status}");
        }
        Ok(true)
    }
}

impl Drop for Encoder {
    fn drop(&mut self) {
        unsafe {
            self.session.invalidate();
            // The callback cannot fire after invalidate returns.
            drop(Box::from_raw(self.callback_context));
        }
    }
}

/// Parameter sets from the format description as Annex B.
fn parameter_sets(description: &CMFormatDescription) -> Option<Vec<u8>> {
    let mut out = Vec::new();
    let mut count: usize = 0;
    let mut index = 0;
    loop {
        let mut pointer: *const u8 = ptr::null();
        let mut size: usize = 0;
        let mut header_length: i32 = 0;
        let status = unsafe {
            CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(
                description, index, &mut pointer, &mut size, &mut count, &mut header_length,
            )
        };
        if status != 0 || pointer.is_null() {
            return None;
        }
        out.extend_from_slice(&ANNEX_B_START_CODE);
        out.extend_from_slice(unsafe { std::slice::from_raw_parts(pointer, size) });
        index += 1;
        if index >= count {
            break;
        }
    }
    Some(out)
}

/// Rewrites the AVCC/HVCC length-prefixed sample into Annex B start codes.
fn annex_b_from_sample(sample: &CMSampleBuffer) -> Option<Vec<u8>> {
    let block = unsafe { sample.data_buffer()? };
    let mut length_at_offset: usize = 0;
    let mut total_length: usize = 0;
    let mut data: *mut i8 = ptr::null_mut();
    let status = unsafe { block.data_pointer(0, &mut length_at_offset, &mut total_length, &mut data) };
    if status != 0 || data.is_null() || length_at_offset != total_length {
        // Non-contiguous block buffers are not something VideoToolbox produces
        // for encoded frames in practice; refuse rather than mis-parse.
        return None;
    }
    let bytes = unsafe { std::slice::from_raw_parts(data as *const u8, total_length) };

    let mut out = Vec::with_capacity(total_length + 16);
    let mut offset = 0;
    while offset + 4 <= bytes.len() {
        let length = u32::from_be_bytes(bytes[offset..offset + 4].try_into().unwrap()) as usize;
        offset += 4;
        if offset + length > bytes.len() {
            return None;
        }
        out.extend_from_slice(&ANNEX_B_START_CODE);
        out.extend_from_slice(&bytes[offset..offset + length]);
        offset += length;
    }
    Some(out)
}

fn is_sync_sample(sample: &CMSampleBuffer) -> bool {
    let Some(attachments) = (unsafe { sample.sample_attachments_array(false) }) else {
        return true;
    };
    if attachments.count() == 0 {
        return true;
    }
    let first = unsafe { attachments.value_at_index(0) } as *const CFDictionary;
    if first.is_null() {
        return true;
    }
    let not_sync = unsafe { (*first).value(kCMSampleAttachmentKey_NotSync as *const CFString as *const c_void) }
        as *const CFBoolean;
    if not_sync.is_null() {
        return true;
    }
    !unsafe { (*not_sync).as_bool() }
}

unsafe extern "C-unwind" fn output_callback(
    refcon: *mut c_void,
    frame_refcon: *mut c_void,
    status: i32,
    _flags: VTEncodeInfoFlags,
    sample: *mut CMSampleBuffer,
) {
    let context = &*(refcon as *const CallbackContext);
    let frame = Box::from_raw(frame_refcon as *mut FrameContext);
    if status != 0 || sample.is_null() {
        log::error!("encoder output failed: {status}");
        return;
    }
    let sample = &*sample;
    let Some(nals) = annex_b_from_sample(sample) else {
        log::error!("could not read encoded sample");
        return;
    };
    let is_idr = is_sync_sample(sample);
    let config = if is_idr {
        sample.format_description().and_then(|description| parameter_sets(&description))
    } else {
        None
    };
    context
        .sender
        .send(EncodedFrame {
            target_timestamp: frame.target_timestamp,
            is_idr,
            nals,
            config,
            encode_time: frame.submitted_at.elapsed(),
        })
        .ok();
}

#[cfg(test)]
mod benchmark {
    //! How fast the hardware encoder is with busy moving content, by picture size, rate and input
    //! format: `cargo test --release encoder_benchmark -- --ignored --nocapture`.
    use super::*;
    use objc2_core_video::{
        kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, CVPixelBufferGetBaseAddressOfPlane, CVPixelBufferGetBytesPerRowOfPlane,
    };

    /// Detail to pan across: smooth noise at a few scales, like a textured scene.
    fn texture(width: usize, height: usize) -> Vec<u8> {
        let hash = |x: usize, y: usize| -> u32 {
            let mut h = (x as u32).wrapping_mul(0x9e37_79b1) ^ (y as u32).wrapping_mul(0x85eb_ca77);
            h ^= h >> 15;
            h = h.wrapping_mul(0x2c1b_3c6d);
            h ^ (h >> 12)
        };
        (0..width * height)
            .map(|i| {
                let (x, y) = (i % width, i / width);
                let fine = hash(x, y) & 63;
                let middle = hash(x / 8, y / 8) & 127;
                let coarse = hash(x / 64, y / 64) & 63;
                (fine + middle + coarse) as u8
            })
            .collect()
    }

    fn run(width: u32, height: u32, fps: f32, bitrate: u64, nv12: bool, texture: &[u8], texture_width: usize) {
        let input = if nv12 { kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange } else { kCVPixelFormatType_32BGRA };
        let (mut encoder, encoded) = Encoder::with_input(width, height, fps, bitrate, input).unwrap();
        let interval = Duration::from_secs_f32(1.0 / fps);
        let (warmup, measured) = ((fps * 0.5) as u32, (fps * 3.0) as u32);
        let started = Instant::now();
        let mut blocked = Duration::ZERO;
        for frame in 0..warmup + measured {
            let due = started + interval * frame;
            if let Some(wait) = due.checked_duration_since(Instant::now()) {
                std::thread::sleep(wait);
            }
            // Panning: everything moves a few pixels a frame, as with a turning head.
            let (dx, dy) = ((frame as usize * 6) % 512, (frame as usize * 2) % 256);
            let submitted = Instant::now();
            encoder
                .encode_buffer_with(frame == 0, |buffer| unsafe {
                    if nv12 {
                        let luma = CVPixelBufferGetBaseAddressOfPlane(buffer, 0) as *mut u8;
                        let luma_stride = CVPixelBufferGetBytesPerRowOfPlane(buffer, 0);
                        for y in 0..height as usize {
                            let source = &texture[(y + dy) * texture_width + dx..][..width as usize];
                            std::ptr::copy_nonoverlapping(source.as_ptr(), luma.add(y * luma_stride), width as usize);
                        }
                        let chroma = CVPixelBufferGetBaseAddressOfPlane(buffer, 1) as *mut u8;
                        let chroma_stride = CVPixelBufferGetBytesPerRowOfPlane(buffer, 1);
                        for y in 0..height as usize / 2 {
                            let source = &texture[(y * 2 + dy) * texture_width + dx..][..width as usize];
                            std::ptr::copy_nonoverlapping(source.as_ptr(), chroma.add(y * chroma_stride), width as usize);
                        }
                    } else {
                        let base = CVPixelBufferGetBaseAddress(buffer) as *mut u8;
                        let stride = CVPixelBufferGetBytesPerRow(buffer);
                        for y in 0..height as usize {
                            let row = base.add(y * stride);
                            let source = &texture[(y + dy) * texture_width + dx..][..width as usize];
                            for (x, &v) in source.iter().enumerate() {
                                *row.add(x * 4) = v;
                                *row.add(x * 4 + 1) = v.wrapping_add(40);
                                *row.add(x * 4 + 2) = v.wrapping_mul(3);
                                *row.add(x * 4 + 3) = 255;
                            }
                        }
                    }
                    Some(Duration::ZERO)
                })
                .unwrap();
            if frame >= warmup {
                blocked += submitted.elapsed();
            }
        }
        drop(encoder);
        let frames: Vec<EncodedFrame> = encoded.try_iter().collect();
        let mut latencies: Vec<f64> = frames.iter().skip(warmup as usize).map(|f| f.encode_time.as_secs_f64() * 1000.0).collect();
        latencies.sort_by(f64::total_cmp);
        let bytes: usize = frames.iter().skip(warmup as usize).map(|f| f.nals.len()).sum();
        let mean = latencies.iter().sum::<f64>() / latencies.len().max(1) as f64;
        let p95 = latencies.get(latencies.len() * 95 / 100).copied().unwrap_or(f64::NAN);
        println!(
            "{width}x{height} {fps:>3} Hz ({:>3} Mbps) {:<4}: {:>4} of {measured} frames, encode mean {mean:5.1} ms p95 {p95:5.1} ms, submit+fill {:4.1} ms, {:5.1} Mbps",
            bitrate / 1_000_000,
            if nv12 { "NV12" } else { "BGRA" },
            latencies.len(),
            blocked.as_secs_f64() * 1000.0 / f64::from(measured),
            bytes as f64 * 8.0 / 3.0 / 1e6,
        );
    }

    #[test]
    #[ignore]
    fn encoder_benchmark() {
        let texture_width = 4128 + 512;
        let texture = texture(texture_width, 2208 + 256);
        for (width, height) in [(3328, 1792), (4128, 2208)] {
            for fps in [72.0, 90.0, 120.0] {
                for nv12 in [false, true] {
                    run(width, height, fps, 60_000_000, nv12, &texture, texture_width);
                }
            }
        }
    }
}
