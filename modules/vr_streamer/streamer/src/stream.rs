//! The frame pump: waits for the game to publish a frame, copies it into
//! the encoder, and hands the encoded NAL units to the headset (`Headset`).

use crate::{
    encoder::{EncodedFrame, Encoder},
    shared_frames::{self, HapticPulse, SharedFrames},
};
use log::{error, info, warn};
use std::{
    fs::File,
    io::Write,
    path::Path,
    sync::{
        atomic::{AtomicBool, AtomicU64, Ordering},
        mpsc::RecvTimeoutError,
        Arc,
    },
    thread,
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};

/// Where frames and vibrations go.
pub trait Headset: Send + Sync + 'static {
    /// The picture the encoder makes: both eyes, the left above the right.
    fn picture(&self) -> (u32, u32);
    /// Sends an encoded frame; false when it couldn't go (before the first keyframe).
    fn send_frame(&self, frame: EncodedFrame) -> bool;
    /// A vibration the game asked for on a hand (0 left, 1 right).
    fn send_haptics(&self, hand: usize, pulse: &HapticPulse);
    /// A bitrate the headset's side wants now, if it has a say.
    fn bitrate_bps(&self) -> Option<u64> {
        None
    }
    /// The frame drawn for this tracking timestamp went to the encoder.
    fn presented(&self, _target: Duration) {}
    /// The field of view (left, right, up, down angles) the frame for this tracking timestamp was
    /// drawn with, before it goes to the encoder.
    fn rendered_with(&self, _tracking_ns: u64, _fov: [[f32; 4]; 2]) {}
}

/// State the event loop shares with the pump.
pub struct StreamControl {
    pub running: AtomicBool,
    /// Latest tracking timestamp from the client, in nanoseconds. Used to stamp
    /// frames only while the game has not yet reported which sample it
    /// rendered with.
    pub latest_tracking_ns: AtomicU64,
    pub idr_requested: AtomicBool,
    /// Head pose and optics for the game, written into the shared buffer
    /// by the pump whenever the event loop updates them.
    pub host_state: parking_lot::Mutex<shared_frames::HostState>,
    pub host_state_dirty: AtomicBool,
    /// The controllers for the game, written like the host state.
    pub input_state: parking_lot::Mutex<[shared_frames::ControllerState; 2]>,
    pub input_state_dirty: AtomicBool,
}

/// The field of view of the centre of a picture `drawn` pixels across that's cropped to `kept`:
/// what the headset is sent when the game draws larger eyes than the stream carries (left, right,
/// up and down angles).
fn cropped_fov(fov: [[f32; 4]; 2], drawn: (u32, u32), kept: (u32, u32)) -> [[f32; 4]; 2] {
    let crop = |low: f32, high: f32, drawn: u32, kept: u32| {
        let (low, high) = (low.tan(), high.tan());
        let cut = (high - low) * (drawn.saturating_sub(kept)) as f32 / 2.0 / drawn.max(1) as f32;
        ((low + cut).atan(), (high - cut).atan())
    };
    fov.map(|[left, right, up, down]| {
        let (left, right) = crop(left, right, drawn.0, kept.0);
        let (down, up) = crop(down, up, drawn.1, kept.1);
        [left, right, up, down]
    })
}

/// When the game draws nothing for this long (loading, a hitch), its last picture is encoded again
/// and sent, so the headset keeps the stream and holds the picture instead of dropping the link,
/// as SteamVR's own compositor keeps showing something while a game is busy. Again every
/// REPEAT_EVERY until the game draws.
const REPEAT_AFTER: Duration = Duration::from_millis(100);
const REPEAT_EVERY: Duration = Duration::from_millis(50);

fn wall_now_unix_ns() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_nanos() as u64).unwrap_or(0)
}

pub fn run(
    headset: Arc<dyn Headset>,
    control: Arc<StreamControl>,
    frames_path: &Path,
    eye: (u32, u32),
    fps: f32,
    bitrate_bps: u64,
) {
    let (view_width, view_height) = eye;
    let (width, height) = headset.picture();
    let (mut encoder, encoded) = match Encoder::new(width, height, fps, bitrate_bps) {
        Ok(pair) => pair,
        Err(e) => {
            error!("encoder: {e}");
            return;
        }
    };
    info!("encoder ready: HEVC {width}x{height} @ {fps} fps, {} Mbps", bitrate_bps / 1_000_000);

    // Deliver encoded frames from their own thread so a slow network path never blocks the
    // encode submission.
    let sender_thread = thread::spawn({
        let headset = Arc::clone(&headset);
        let control = Arc::clone(&control);
        move || {
            // VR_STREAMER_RECORD=<path> writes the raw Annex B stream to disk so
            // the encoder output can be checked with ffmpeg without a headset.
            let mut recording = std::env::var_os("VR_STREAMER_RECORD").and_then(|path| match File::create(&path) {
                Ok(file) => {
                    info!("recording stream to {}", path.to_string_lossy());
                    Some(file)
                }
                Err(e) => {
                    warn!("cannot record to {}: {e}", path.to_string_lossy());
                    None
                }
            });
            let mut stats_started = Instant::now();
            let mut stats_seconds = 0u32;
            let mut stats_frames = 0u32;
            let mut stats_bytes = 0usize;
            let mut stats_encode = Duration::ZERO;
            while control.running.load(Ordering::Relaxed) {
                let frame = match encoded.recv_timeout(Duration::from_millis(100)) {
                    Ok(frame) => frame,
                    Err(RecvTimeoutError::Timeout) => continue,
                    Err(RecvTimeoutError::Disconnected) => break,
                };
                if let Some(file) = &mut recording {
                    if let Some(config) = &frame.config {
                        file.write_all(config).ok();
                    }
                    file.write_all(&frame.nals).ok();
                }
                let (bytes, encode_time) = (frame.nals.len(), frame.encode_time);
                if !headset.send_frame(frame) {
                    continue;
                }
                stats_frames += 1;
                stats_bytes += bytes;
                stats_encode += encode_time;

                // Each second on stdout; every fifth in the log.
                if stats_started.elapsed() >= Duration::from_secs(1) {
                    let seconds = stats_started.elapsed().as_secs_f64();
                    let fps = stats_frames as f64 / seconds;
                    let mbps = stats_bytes as f64 * 8.0 / seconds / 1e6;
                    let encode_ms = stats_encode.as_secs_f64() * 1000.0 / stats_frames.max(1) as f64;
                    crate::status::status("stats", serde_json::json!({ "fps": fps, "mbps": mbps, "encode_ms": encode_ms }));
                    stats_seconds += 1;
                    if stats_seconds % 5 == 0 {
                        info!("encoded {fps:.1} fps, {mbps:.1} Mbps, encode {encode_ms:.1} ms");
                    }
                    stats_started = Instant::now();
                    stats_frames = 0;
                    stats_bytes = 0;
                    stats_encode = Duration::ZERO;
                }
            }
        }
    });

    let mut frames: Option<SharedFrames> = None;
    let mut last_frame_id = 0u32;
    let mut first_frame = true;
    let mut last_submitted = Instant::now();
    let mut repeating_since: Option<Instant> = None; // when the game last drew, while repeating
    let mut size_warned = false;
    // The haptic serials already sent, by hand.
    let mut haptics_seen = [0u64; 2];
    let mut published_at_stats: Option<u64> = None;
    let mut submit_stats_started = Instant::now();
    let mut submitted = 0u32;
    let mut copy_total = Duration::ZERO;

    while control.running.load(Ordering::Relaxed) {
        // The game makes the file with the game's first frame and takes it
        // away when VR stops.
        if frames.as_ref().is_none_or(|f| !f.alive()) {
            frames = match SharedFrames::open(frames_path.to_owned()) {
                Ok(opened) => {
                    info!("reading frames: {}x{} per eye, {:?}", opened.eye_width, opened.eye_height, opened.format);
                    first_frame = true;
                    Some(opened)
                }
                Err(e) => {
                    let message = e.to_string();
                    if message != "not published yet" && !message.contains("No such file") {
                        warn!("cannot open {}: {e}", frames_path.display());
                    }
                    None
                }
            };
        }

        let Some(shared) = &frames else {
            thread::sleep(Duration::from_millis(250));
            continue;
        };
        shared.heartbeat();
        if control.host_state_dirty.swap(false, Ordering::AcqRel) {
            let state = *control.host_state.lock();
            shared.write_host_state(&state);
        }
        if control.input_state_dirty.swap(false, Ordering::AcqRel) {
            let hands = *control.input_state.lock();
            shared.write_input_state(&hands);
        }
        // Vibrations the game asked for since the last look go to the headset.
        if let Some(pulses) = shared.read_haptics() {
            for (hand, pulse) in pulses.iter().enumerate() {
                if pulse.serial != haptics_seen[hand] {
                    haptics_seen[hand] = pulse.serial;
                    headset.send_haptics(hand, pulse);
                }
            }
        }

        if shared.eye_width < view_width || shared.eye_height < view_height {
            if !size_warned {
                error!(
                    "the game publishes {}x{} per eye but the stream needs at least {}x{}; frames are not sent",
                    shared.eye_width, shared.eye_height, view_width, view_height
                );
                size_warned = true;
            }
            thread::sleep(Duration::from_millis(250));
            continue;
        }

        match shared.latest_frame_id() {
            Some(id) if id != last_frame_id => {
                last_frame_id = id;
                if let Some(since) = repeating_since.take() {
                    info!("the game draws again after {:.1} s", since.elapsed().as_secs_f64());
                }
            }
            // The same picture again: re-encoded, as a frame that changes nothing (sending the last
            // encoded frame twice would build it on itself).
            Some(_) if !first_frame && last_submitted.elapsed() >= if repeating_since.is_some() { REPEAT_EVERY } else { REPEAT_AFTER } => {
                if repeating_since.is_none() {
                    info!("the game has drawn nothing for {} ms; repeating its last picture", REPEAT_AFTER.as_millis());
                    repeating_since = Some(last_submitted);
                }
            }
            _ => {
                thread::sleep(Duration::from_millis(1));
                continue;
            }
        }

        if let Some(bitrate) = headset.bitrate_bps() {
            encoder.set_bitrate(bitrate);
        }

        let force_idr = control.idr_requested.swap(false, Ordering::Relaxed) || first_frame;
        let mut submitted_at_unix_ns = 0u64;
        let mut frame_id = 0u32;
        let mut copy_time = Duration::ZERO;
        // Stamp the frame with the tracking sample the game rendered it from;
        // the headset reprojects from that pose to the pose at display time.
        let mut target_timestamp = Duration::from_nanos(control.latest_tracking_ns.load(Ordering::Relaxed));
        let result = encoder.encode_with(force_idr, |out, stride| {
            let started = Instant::now();
            let copied = shared.copy_latest_bgra(out, width, height, stride);
            copy_time = started.elapsed();
            match copied {
                Some(info) => {
                    headset.rendered_with(info.tracking_timestamp_ns, cropped_fov(info.fov, (shared.eye_width, shared.eye_height), eye));
                    submitted_at_unix_ns = info.submitted_at_unix_ns;
                    frame_id = info.frame_id;
                    if info.tracking_timestamp_ns != 0 {
                        target_timestamp = Duration::from_nanos(info.tracking_timestamp_ns);
                    }
                    Some(target_timestamp)
                }
                None => None,
            }
        });
        match result {
            Ok(true) => {
                last_submitted = Instant::now();
                if first_frame {
                    let age_ms = wall_now_unix_ns().saturating_sub(submitted_at_unix_ns) as f64 / 1e6;
                    info!("first frame (game frame {frame_id}) submitted to the encoder {age_ms:.1} ms after the game submitted it");
                    first_frame = false;
                }
                headset.presented(target_timestamp);
                submitted += 1;
                copy_total += copy_time;
                if submit_stats_started.elapsed() >= Duration::from_secs(5) {
                    let seconds = submit_stats_started.elapsed().as_secs_f64();
                    let published = shared.frames_published();
                    let game_fps = published_at_stats.map_or(f64::NAN, |before| published.saturating_sub(before) as f64 / seconds);
                    published_at_stats = Some(published);
                    info!(
                        "game published {game_fps:.1} fps, submitted {:.1} fps, copy into pixel buffer {:.1} ms, game dropped {} so far",
                        submitted as f64 / seconds,
                        copy_total.as_secs_f64() * 1000.0 / submitted.max(1) as f64,
                        shared.frames_dropped()
                    );
                    submit_stats_started = Instant::now();
                    submitted = 0;
                    copy_total = Duration::ZERO;
                }
            }
            Ok(false) => {
                // Torn read: the game was mid-write. The next poll gets it.
                control.idr_requested.fetch_or(force_idr, Ordering::Relaxed);
            }
            Err(e) => {
                error!("encode: {e}");
                thread::sleep(Duration::from_millis(100));
            }
        }
    }

    drop(encoder);
    sender_thread.join().ok();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cropping_narrows_the_field_of_view() {
        let fov = [[-0.8f32, 0.7, 0.75, -0.9]; 2];
        assert_eq!(cropped_fov(fov, (2000, 2000), (2000, 2000)), fov);
        let cropped = cropped_fov(fov, (2000, 2000), (1000, 1000));
        let [left, right, up, down] = cropped[0];
        // Half the picture across: half the span of tangents, about the same middle.
        assert!(((right.tan() - left.tan()) - (0.7f32.tan() - (-0.8f32).tan()) / 2.0).abs() < 1e-5);
        assert!(((right.tan() + left.tan()) - (0.7f32.tan() + (-0.8f32).tan())).abs() < 1e-5);
        assert!(((up.tan() - down.tan()) - (0.75f32.tan() - (-0.9f32).tan()) / 2.0).abs() < 1e-5);
    }
}
