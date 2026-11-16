//! One headset's stream (OpenVRLink's docs/svl-*): its poses and controllers reach the game, the
//! game's frames and sound reach it. The headset sends a pose packet four times a frame, with its
//! values (field of view, eye positions, refresh rates) riding behind; the host answers with its
//! own values, video, audio and haptics.
//!
//! Clocks: each side's clock is its own microsecond counter. Each estimates
//! `its clock - the other's` from the other's send times (plus a delay); the two estimates
//! together give the true difference, which turns the headset's display times into this host's.

use super::{
    link::{self, class, unreliable, video as video_kind, Element},
    values::Values,
    video::{self, Metadata, VideoSender},
};
use crate::{
    shared_frames::{input, ControllerState, INPUT_SLOTS},
    stream::StreamControl,
};
use log::{info, warn};
use parking_lot::Mutex;
use std::{
    collections::VecDeque,
    net::{IpAddr, SocketAddr, UdpSocket},
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    thread,
    time::{Duration, Instant},
};

/// The headset's values this host reads (docs/svl-values/vte-id-table.md).
mod headset_value {
    pub const PROJECTION: u8 = 0x04;
    /// The play space, as SteamVR's chaperone_info JSON. Sent again when the play space changes,
    /// a recentre among them (its seated pose moves).
    pub const CHAPERONE: u8 = 0x05;
    pub const EYE_TO_HEAD: u8 = 0x08;
    pub const FRAME_TIME: u8 = 0x0c;
    pub const RATES: u8 = 0x20;
    pub const VERSION: u8 = 0x22;
}

/// The host's values, as Steam's host sends them where the meaning is known.
mod host_value {
    pub const INSTANCE: u8 = 0x02;
    pub const COLOR_CONTROL: u8 = 0x17;
    pub const AUDIO_FEC: u8 = 0x19;
    pub const UNGUIDED_RETRANSMIT: u8 = 0x1a;
    pub const PING_DECAY: u8 = 0x1c;
    pub const RATE_REQUEST: u8 = 0x21;
    pub const AUDIO_JITTER: u8 = 0x26;
    pub const WIRELESS_TIMEOUT: u8 = 0x27;
    pub const HAND_TRACKING: u8 = 0x2e;
    pub const APPLICATION_RUNNING: u8 = 0x35;
    /// Any change makes the headset look at its play space again and send it (CHAPERONE).
    pub const CHAPERONE_REQUEST: u8 = 0x16;
}

/// The jitter-buffer settings Steam's host sends (docs/svl-audio/negotiation-and-settings.md).
const AUDIO_JITTER_SETTINGS: [u8; 47] = [
    0x01, 0x01, 0x01, 0x32, 0, 0, 0, 0x19, 0, 0, 0, 0x0f, 0, 0, 0, 0x14, 0, 0, 0, 0x07, 0, 0, 0, 0xf4, 0x01, 0, 0, 0xf4, 0x01, 0, 0, 0x02, 0, 0, 0,
    0x32, 0, 0, 0, 0x0a, 0, 0, 0, 0x05, 0, 0, 0,
];
const POSE_BLOCK: usize = 0x168;
const PACKED_POSE: usize = 88;
/// The headset has gone when nothing has come from it for this long.
const SILENCE: Duration = Duration::from_secs(5);
/// Q parity goes with the video for this long after a session starts and after the headset
/// reports a loss, as Steam's host does.
const Q_AFTER_LOSS: Duration = Duration::from_secs(10);
/// How far ahead of a pose's arrival its frame is shown, at first: the lead then follows what the
/// headset reports (`Latency`).
const FIRST_LEAD_US: f32 = 40_000.0;
/// The lead stays within this.
const MOST_LEAD_US: f32 = 100_000.0;
/// How often the stages' times go to the log.
const REPORT_EVERY: Duration = Duration::from_secs(5);
/// How often the headset is asked for its play space, which a recentre changes.
const CHAPERONE_EVERY: Duration = Duration::from_secs(2);
/// Each eye's panel, by the headset's name as it pairs (rounded up to 32, as the runtime takes it):
/// the size each eye is drawn at from the next start. The first that the name contains.
const PANELS: [(&str, (u32, u32)); 6] = [
    ("Quest 3S", (1856, 1920)),
    ("Quest 3", (2080, 2208)),
    ("Quest Pro", (1824, 1920)),
    ("Quest 2", (1856, 1920)),
    ("Frame", (2176, 2176)),
    ("Quest", (1440, 1600)),
];

pub fn panel_size(device_name: &str) -> Option<(u32, u32)> {
    PANELS.iter().find(|(name, _)| device_name.contains(name)).map(|&(_, size)| size)
}

/// Audio packets: 256 stereo frames, the most error correction would cover.
pub const AUDIO_FRAMES: usize = 256;

/// The headset's estimate of `its clock - ours` and ours of `ours - its`, with the confidence
/// rules of `SVLRoot::SymmetricTimingUpdate` (docs/svl-timing/clock-sync.md).
#[derive(Default)]
struct Clock {
    offset: i32,
    confidence: i32,
    peer_offset: i32,
}

impl Clock {
    fn update(&mut self, peer_time: i32, peer_offset: i32, local_time: i32) {
        self.peer_offset = peer_offset;
        let difference = local_time.wrapping_sub(peer_time);
        if self.confidence == 0 {
            self.offset = difference;
            self.confidence = 1;
            return;
        }
        let k = 1_000_000 / (self.confidence + 5);
        let error = difference.wrapping_sub(self.offset);
        if error <= 0 {
            if error >= -(k / 2) {
                // In 64 bits: error and k each reach 200 000, and their product overflows 32.
                self.offset = self.offset.wrapping_sub((-i64::from(error) * i64::from(k) / 200_000).min(100) as i32);
                self.confidence = (self.confidence + 1).min(400);
            } else if self.confidence < 20 {
                self.confidence = 0;
            } else {
                self.confidence -= 4;
            }
        } else if error > k {
            self.confidence = (self.confidence - 1).max(0);
        } else {
            self.offset = self.offset.wrapping_add((i64::from(error) * i64::from(k) / 2_000_000).min(5) as i32);
            self.confidence = (self.confidence + 1).min(400);
        }
    }

    /// A time on the headset's clock as this host's.
    fn to_host(&self, headset_time: i32) -> i32 {
        // Halved in 64 bits: the two offsets are near opposites, so their 32-bit difference wraps
        // whenever the offset is over a quarter of the range, and halving the wrapped value put the
        // time half the range (35 minutes) out.
        let difference = if self.peer_offset != 0 { ((i64::from(self.offset) - i64::from(self.peer_offset)) / 2) as i32 } else { self.offset };
        headset_time.wrapping_add(difference)
    }
}

/// This host's clock: microseconds since the streamer started, wrapping.
fn now_us() -> i32 {
    static START: std::sync::OnceLock<Instant> = std::sync::OnceLock::new();
    START.get_or_init(Instant::now).elapsed().as_micros() as u32 as i32
}

type Quaternion = [f32; 4]; // x, y, z, w

fn multiply(a: Quaternion, b: Quaternion) -> Quaternion {
    let [ax, ay, az, aw] = a;
    let [bx, by, bz, bw] = b;
    [
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    ]
}

/// A pose carried `seconds` ahead by its velocities; the angular one is about the device's own axes.
fn predict(orientation: Quaternion, position: [f32; 3], linear: [f32; 3], angular: [f32; 3], seconds: f32) -> (Quaternion, [f32; 3]) {
    let angle = (angular[0] * angular[0] + angular[1] * angular[1] + angular[2] * angular[2]).sqrt() * seconds;
    let turned = if angle > 1e-6 {
        let axis = angular.map(|a| a * seconds / angle);
        let (sin, cos) = (angle / 2.0).sin_cos();
        let mut q = multiply(orientation, [axis[0] * sin, axis[1] * sin, axis[2] * sin, cos]);
        let length = q.iter().map(|v| v * v).sum::<f32>().sqrt();
        q.iter_mut().for_each(|v| *v /= length);
        q
    } else {
        orientation
    };
    (turned, std::array::from_fn(|i| position[i] + linear[i] * seconds))
}

struct Pose {
    orientation: Quaternion,
    position: [f32; 3],
    linear: [f32; 3],
    angular: [f32; 3],
    valid: bool,
}

fn f32_at(bytes: &[u8], offset: usize) -> f32 {
    f32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap())
}

fn i32_at(bytes: &[u8], offset: usize) -> i32 {
    i32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap())
}

/// `PackedPose_t`: position, orientation (w first), velocities, tracking result and state.
fn packed_pose(bytes: &[u8]) -> Pose {
    let f = |i: usize| f32_at(bytes, i);
    Pose {
        position: [f(0x00), f(0x04), f(0x08)],
        orientation: [f(0x10), f(0x14), f(0x18), f(0x0c)],
        linear: [f(0x1c), f(0x20), f(0x24)],
        angular: [f(0x28), f(0x2c), f(0x30)],
        valid: bytes[0x36] == 2,
    }
}

/// One hand's input block (docs/svl-tracking-input/controllers.md) as the Touch controller's inputs.
fn touch_inputs(block: &[u8], right: bool) -> [f32; INPUT_SLOTS] {
    let clicks = u64::from_le_bytes(block[0..8].try_into().unwrap());
    let touches = u64::from_le_bytes(block[8..16].try_into().unwrap());
    let bit = |bits: u64, i: u32| (bits >> i & 1) as f32;
    let axis = |i: usize| (block[0x11 + i] as i8 as f32 / 127.0).clamp(-1.0, 1.0);
    let mut inputs = [0.0; INPUT_SLOTS];
    let (lower, lower_touch, upper, upper_touch) = if right {
        (input::A_CLICK, input::A_TOUCH, input::B_CLICK, input::B_TOUCH)
    } else {
        (input::X_CLICK, input::X_TOUCH, input::Y_CLICK, input::Y_TOUCH)
    };
    inputs[lower] = bit(clicks, 0);
    inputs[upper] = bit(clicks, 1);
    if !right {
        inputs[input::MENU_CLICK] = bit(clicks, 2);
    }
    inputs[input::THUMBSTICK_CLICK] = bit(clicks, 3);
    inputs[lower_touch] = bit(touches, 0);
    inputs[upper_touch] = bit(touches, 1);
    inputs[input::TRIGGER_TOUCH] = bit(touches, 2);
    inputs[input::THUMBSTICK_TOUCH] = bit(touches, 3);
    inputs[input::THUMBREST_TOUCH] = bit(touches, 4);
    inputs[input::TRIGGER_VALUE] = axis(0).max(0.0);
    inputs[input::THUMBSTICK_X] = axis(1);
    inputs[input::THUMBSTICK_Y] = axis(2);
    inputs[input::SQUEEZE_VALUE] = axis(3).max(0.0);
    inputs
}

/// What a frame was drawn for, kept until its frame goes out.
struct Rendered {
    tracking_ns: u64,
    orientation: Quaternion,
    position: [f32; 3],
    /// When the pose arrived and when its frame should be seen, on this host's clock.
    arrived: i32,
    display_time: i32,
    /// The field of view the game drew the frame with (left, right, up, down angles), once known.
    fov: Option<[[f32; 4]; 2]>,
}

/// How long each stage takes, from the headset's frame reports (docs/svl-timing/frame-reports.md),
/// in microseconds; and the lead (from a pose's arrival to its frame on the display) that poses
/// are predicted by and frames stamped with, as Steam's host adapts its own. The lead follows when
/// frames are decoded on the headset, plus a display period: not when they're shown, which is when
/// the headset was told to show them (a late stamp would only ever grow).
struct Latency {
    lead: f32,
    /// The headset's display period.
    period: f32,
    /// Frames sent: id, when, and when the pose it was drawn from arrived (host clock).
    sent: VecDeque<(u16, i32, i32)>,
    last_accepted: u16,
    last_decoded: u16,
    last_shown: u16,
    // Sums since the last report: pose to sent (game, copy and encoding), sent to complete on the
    // headset (network), complete to decoded, and pose to shown.
    sums: [f64; 4],
    counts: [u32; 4],
    frames: u32,
    reported: Instant,
    /// Frames the headset has taken off the network: id, and when (the headset's clock), so a
    /// later report's decode time can be measured against its own frame.
    accepted: VecDeque<(u16, i32)>,
    // Since the last report: frame reports from the headset, network times outside 0 to 1 s (the
    // last of them: a clock estimate gone wrong shows here), lead updates from the headset's clock,
    // retransmissions asked for and decoder resets.
    reports: u32,
    network_outside: u32,
    network_raw: i32,
    clock_leads: u32,
    retransmits: u32,
    resets: u32,
    /// The headset's own ping to this host (its statistics), when it has said.
    ping_us: Option<f32>,
    /// The headset's share of packets received, the last it said.
    pass: Option<f64>,
}

impl Latency {
    fn new() -> Self {
        Self {
            lead: FIRST_LEAD_US,
            period: 1e6 / 72.0,
            sent: VecDeque::new(),
            last_accepted: 0,
            last_decoded: 0,
            last_shown: 0,
            sums: [0.0; 4],
            counts: [0; 4],
            frames: 0,
            reported: Instant::now(),
            accepted: VecDeque::new(),
            reports: 0,
            network_outside: 0,
            network_raw: 0,
            clock_leads: 0,
            retransmits: 0,
            resets: 0,
            ping_us: None,
            pass: None,
        }
    }

    fn add(&mut self, stage: usize, us: i32) {
        if (0..1_000_000).contains(&us) {
            self.sums[stage] += f64::from(us);
            self.counts[stage] += 1;
        }
    }

    fn sent_frame(&self, id: u16) -> Option<(i32, i32)> {
        self.sent.iter().find(|(i, _, _)| *i == id).map(|&(_, sent, arrived)| (sent, arrived))
    }

    fn report(&mut self, bitrate_bps: u64) {
        if self.reported.elapsed() < REPORT_EVERY {
            return;
        }
        let average = |sum: f64, count: u32| if count == 0 { f64::NAN } else { sum / f64::from(count) };
        let ms = |stage: usize| average(self.sums[stage], self.counts[stage]) / 1000.0;
        // Without a lead from the headset's clock, one from what doesn't need it: this host's own
        // pose-to-sent, half the headset's ping, its decode time and a display period.
        if self.clock_leads == 0 && self.counts[0] > 0 {
            if let Some(ping) = self.ping_us {
                let decode = if self.counts[2] > 0 { average(self.sums[2], self.counts[2]) as f32 } else { 5_000.0 };
                let target = average(self.sums[0], self.counts[0]) as f32 + ping / 2.0 + decode + self.period;
                self.lead = (self.lead + (target - self.lead) * 0.5).clamp(self.period, MOST_LEAD_US);
            }
        }
        info!(
            "Steam Link: {:.1} fps at {:.0} Mbps; pose to frame sent {:.1} ms, network {:.1} ms, decode {:.1} ms, pose to display {:.1} ms (lead {:.1} ms)",
            f64::from(self.frames) / self.reported.elapsed().as_secs_f64(),
            bitrate_bps as f64 / 1e6,
            ms(0),
            ms(1),
            ms(2),
            ms(3),
            self.lead / 1000.0
        );
        info!(
            "Steam Link: {} frame reports, {} network times outside 0 to 1 s (last {:.1} ms), {} lead updates from the headset's clock; ping {:.1} ms, packets received {:.2} %, {} retransmissions asked for, {} decoder resets",
            self.reports,
            self.network_outside,
            self.network_raw as f64 / 1000.0,
            self.clock_leads,
            self.ping_us.map_or(f32::NAN, |p| p / 1000.0),
            self.pass.unwrap_or(f64::NAN),
            self.retransmits,
            self.resets
        );
        self.sums = [0.0; 4];
        self.counts = [0; 4];
        self.frames = 0;
        self.reports = 0;
        self.network_outside = 0;
        self.clock_leads = 0;
        self.retransmits = 0;
        self.resets = 0;
        self.reported = Instant::now();
    }
}

struct Shared {
    socket: UdpSocket,
    session_id: u32,
    headset: Mutex<Option<SocketAddr>>,
    sequence: Mutex<u16>,
    values: Mutex<Values>,
    video: Mutex<VideoSender>,
    clock: Mutex<Clock>,
    rendered: Mutex<VecDeque<Rendered>>,
    latency: Mutex<Latency>,
    tangents: Mutex<[[f32; 4]; 2]>,
    q_until: Mutex<Instant>,
    haptic_sequence: Mutex<u16>,
    audio_sequence: Mutex<u16>,
    /// The stream's bitrate, lowered on loss and raised again (rate.rs).
    rate: Mutex<super::rate::Rate>,
    ended: AtomicBool,
}

impl Shared {
    fn send(&self, elements: &[Element]) {
        let Some(headset) = *self.headset.lock() else { return };
        let mut sequence = self.sequence.lock();
        let datagram = link::pack(self.session_id, *sequence, elements);
        *sequence = sequence.wrapping_add(1);
        // A full send buffer drops the datagram: the headset asks again, or error correction covers it.
        self.socket.send_to(&datagram, headset).ok();
    }

    fn send_values(&self) {
        let bytes = {
            let mut values = self.values.lock();
            if !values.pending() {
                return;
            }
            values.tick(link::MAX_VALUE)
        };
        if !bytes.is_empty() {
            self.send(&[Element::new(class::UNRELIABLE, unreliable::VALUES, bytes)]);
        }
    }
}

/// The game's side of a session, handed to the frame pump.
pub struct Session {
    shared: Arc<Shared>,
    pub atlas: (u32, u32),
    frames_sent: std::sync::atomic::AtomicU32,
    keyframe_sent: AtomicBool,
}

impl crate::stream::Headset for Session {
    fn picture(&self) -> (u32, u32) {
        self.atlas
    }

    fn send_frame(&self, frame: crate::encoder::EncodedFrame) -> bool {
        // The headset's decoder starts at a keyframe, which carries its parameter sets in front.
        if !frame.is_idr && !self.keyframe_sent.load(Ordering::Relaxed) {
            return false;
        }
        self.keyframe_sent.store(true, Ordering::Relaxed);
        let mut data = frame.config.unwrap_or_default();
        data.extend_from_slice(&frame.nals);
        let number = self.frames_sent.fetch_add(1, Ordering::Relaxed);
        self.send_frame(&data, frame.target_timestamp.as_nanos() as u64, number);
        true
    }

    fn send_haptics(&self, hand: usize, pulse: &crate::shared_frames::HapticPulse) {
        Session::send_haptics(self, hand, pulse.duration_s, pulse.frequency, pulse.amplitude);
    }

    fn bitrate_bps(&self) -> Option<u64> {
        Some(self.shared.rate.lock().current())
    }

    fn rendered_with(&self, tracking_ns: u64, fov: [[f32; 4]; 2]) {
        if let Some(rendered) = self.shared.rendered.lock().iter_mut().find(|r| r.tracking_ns == tracking_ns) {
            rendered.fov = Some(fov);
        }
    }
}

impl Session {
    /// Starts talking to the headset that was told to come to `start.socket`.
    pub fn start(start: super::host::SessionStart, control: Arc<StreamControl>, eye: (u32, u32), fps: f32, bitrate_bps: u64, controllers: bool, data: std::path::PathBuf) -> Arc<Self> {
        let shared = Arc::new(Shared {
            socket: start.socket,
            session_id: link::session_id(&start.token),
            headset: Mutex::new(None),
            sequence: Mutex::new(0),
            values: Mutex::new(Values::default()),
            video: Mutex::new(VideoSender::default()),
            clock: Mutex::new(Clock::default()),
            rendered: Mutex::new(VecDeque::new()),
            latency: Mutex::new(Latency::new()),
            tangents: Mutex::new([[-1.0, 1.0, -1.0, 1.0]; 2]),
            q_until: Mutex::new(Instant::now() + Q_AFTER_LOSS),
            haptic_sequence: Mutex::new(0),
            audio_sequence: Mutex::new(0),
            rate: Mutex::new(super::rate::Rate::new(bitrate_bps)),
            ended: AtomicBool::new(false),
        });
        shared.socket.set_read_timeout(Some(Duration::from_millis(2))).ok();
        {
            let mut values = shared.values.lock();
            values.publish(host_value::INSTANCE, super::crypto::random_bytes::<8>().to_vec(), true);
            values.publish(host_value::AUDIO_FEC, 0u32.to_le_bytes().to_vec(), true);
            values.publish(host_value::UNGUIDED_RETRANSMIT, 1u32.to_le_bytes().to_vec(), true);
            values.publish(host_value::AUDIO_JITTER, AUDIO_JITTER_SETTINGS.to_vec(), true);
            values.publish(host_value::WIRELESS_TIMEOUT, 30u32.to_le_bytes().to_vec(), true);
            values.publish(host_value::HAND_TRACKING, vec![0], true);
            let mut color = vec![0u8; 104];
            color[0] = 3;
            values.publish(host_value::COLOR_CONTROL, color, true);
            values.publish(host_value::PING_DECAY, 1000u32.to_le_bytes().to_vec(), true);
            values.publish(host_value::APPLICATION_RUNNING, 1u32.to_le_bytes().to_vec(), true);
        }
        let session = Arc::new(Self {
            shared: Arc::clone(&shared),
            atlas: (eye.0, eye.1 * 2),
            frames_sent: Default::default(),
            keyframe_sent: AtomicBool::new(false),
        });
        let headset_ip = start.headset;
        let panel = panel_size(&start.device_name);
        thread::Builder::new()
            .name("steam-link-session".into())
            .spawn(move || receive(shared, headset_ip, control, fps, controllers, data, panel))
            .expect("the session's thread");
        session
    }

    pub fn ended(&self) -> bool {
        self.shared.ended.load(Ordering::Relaxed)
    }

    /// A frame the game drew from the pose stamped `tracking_ns`, encoded (parameter sets in front
    /// of a keyframe).
    pub fn send_frame(&self, encoded: &[u8], tracking_ns: u64, frame_number: u32) {
        let shared = &self.shared;
        let (orientation, position, arrived, display_time, fov) = {
            let mut rendered = shared.rendered.lock();
            while rendered.len() > 1 && rendered.front().is_some_and(|r| r.tracking_ns < tracking_ns) {
                rendered.pop_front();
            }
            match rendered.front() {
                Some(r) => (r.orientation, r.position, r.arrived, r.display_time, r.fov),
                None => ([0.0, 0.0, 0.0, 1.0], [0.0; 3], now_us(), now_us(), None),
            }
        };
        // The frustum the frame was really drawn with: the headset maps the picture onto it, and any
        // other would make the picture swim as the head turns.
        let tangents = match fov {
            Some(fov) => fov.map(|[left, right, up, down]| [left.tan(), right.tan(), down.tan(), up.tan()]),
            None => *shared.tangents.lock(),
        };
        let peer_clock = shared.clock.lock().offset;
        let metadata = Metadata {
            present_number: frame_number,
            orientation,
            position,
            tangents,
            display_time: display_time as u32,
            atlas: (self.atlas.0 as u16, self.atlas.1 as u16),
            tile: (self.atlas.0 as u16, (self.atlas.1 / 2) as u16),
        }
        .bytes();
        let q = Instant::now() < *shared.q_until.lock();
        let sent = now_us();
        let (id, elements) = {
            let mut video = shared.video.lock();
            let elements = video.frame(encoded, &metadata, sent as u32, peer_clock, q);
            (elements[0].id, elements)
        };
        {
            let mut latency = shared.latency.lock();
            latency.sent.push_back((id, sent, arrived));
            while latency.sent.len() > 128 {
                latency.sent.pop_front();
            }
            latency.add(0, sent.wrapping_sub(arrived));
            latency.frames += 1;
        }
        // Paced (pacing.rs): bursts across part of the frame's interval, not all at once.
        let datagrams = video::datagrams(elements);
        let sizes: Vec<usize> = datagrams.iter().map(|d| d.iter().map(Element::size).sum()).collect();
        let period = Duration::from_secs_f32(shared.latency.lock().period / 1e6);
        let spread = super::pacing::spread(sizes.iter().sum(), shared.rate.lock().current(), period);
        let start = Instant::now();
        for (datagram, at) in datagrams.iter().zip(super::pacing::schedule(&sizes, spread)) {
            let wait = (start + at).saturating_duration_since(Instant::now());
            if wait >= Duration::from_micros(200) {
                thread::sleep(wait);
            }
            shared.send(datagram);
        }
    }

    /// A vibration on one hand (0 left, 1 right): two copies of one record, as Steam's host
    /// sends them, so one lost datagram doesn't lose it.
    pub fn send_haptics(&self, hand: usize, seconds: f32, frequency: f32, amplitude: f32) {
        let sequence = {
            let mut s = self.shared.haptic_sequence.lock();
            *s = s.wrapping_add(1);
            *s
        };
        let mut record = Vec::with_capacity(19);
        for value in [frequency, seconds, amplitude] {
            record.extend_from_slice(&value.to_le_bytes());
        }
        record.extend_from_slice(&now_us().to_le_bytes());
        record.extend_from_slice(&sequence.to_le_bytes());
        record.push(hand as u8);
        let value = [record.clone(), record].concat();
        self.shared.send(&[Element::new(class::UNRELIABLE, unreliable::HAPTICS, value)]);
    }

    /// Interleaved 16-bit stereo at `rate` Hz, AUDIO_FRAMES frames at most.
    pub fn send_audio(&self, pcm: &[i16], rate: u32) {
        let sequence = {
            let mut s = self.shared.audio_sequence.lock();
            *s = s.wrapping_add(1);
            *s
        };
        let value: Vec<u8> = pcm.iter().flat_map(|s| s.to_le_bytes()).collect();
        // The format word: channels - 1, and the rate / 10 above it.
        let format = (1 | (rate / 10) << 2) as u16;
        self.shared.send(&[Element { class: class::AUDIO, kind: 0, id: sequence, index: format, value }]);
    }
}

/// Sends the game's sound to the headset while the session lasts, as 16-bit stereo in packets of
/// AUDIO_FRAMES. Without the game's audio device, the stream goes on silent.
#[cfg(target_os = "macos")]
pub fn send_game_audio(session: Arc<Session>) {
    thread::Builder::new()
        .name("steam-link-audio".into())
        .spawn(move || {
            let mut pending: Vec<i16> = Vec::with_capacity(AUDIO_FRAMES * 4);
            let sender = Arc::clone(&session);
            let stream = crate::audio::record(move |data, channels, rate| {
                let channels = usize::from(channels.max(1));
                for frame in data.chunks_exact(channels) {
                    let left = frame[0];
                    let right = if channels > 1 { frame[1] } else { left };
                    pending.push((left.clamp(-1.0, 1.0) * 32767.0) as i16);
                    pending.push((right.clamp(-1.0, 1.0) * 32767.0) as i16);
                    if pending.len() == AUDIO_FRAMES * 2 {
                        sender.send_audio(&pending, rate);
                        pending.clear();
                    }
                }
            });
            let stream = match stream {
                Ok(stream) => stream,
                Err(e) => {
                    warn!("Steam Link: no game audio for the headset: {e}");
                    return;
                }
            };
            while !session.ended() {
                thread::sleep(Duration::from_millis(100));
            }
            drop(stream);
        })
        .ok();
}

/// The session's thread: everything from the headset, and the host's values going back.
fn receive(shared: Arc<Shared>, headset_ip: IpAddr, control: Arc<StreamControl>, fps: f32, controllers: bool, data: std::path::PathBuf, panel: Option<(u32, u32)>) {
    let mut buffer = vec![0u8; 2048];
    let mut last_heard = Instant::now();
    let mut reset_counter: Option<u16> = None;
    let mut rate_requested = false;
    let mut tracking_ns = 0u64;
    let mut optics = (None::<[[f32; 4]; 2]>, None::<f32>);
    let mut seated: Option<Seated> = None;
    let mut chaperone_asked = (Instant::now(), 0u32);
    while !shared.ended.load(Ordering::Relaxed) {
        if last_heard.elapsed() > SILENCE {
            info!("Steam Link: the headset has gone quiet");
            break;
        }
        {
            let mut rate = shared.rate.lock();
            // A step at a time, in each report; the log says when it's back.
            if rate.tick(Instant::now()).is_some_and(|_| rate.is_at_target()) {
                info!("Steam Link: no loss for a while; bitrate back to {:.0} Mbps", rate.current() as f64 / 1e6);
            }
        }
        // Not every headset sends its play space again by itself after a recentre: it's asked.
        if chaperone_asked.0.elapsed() >= CHAPERONE_EVERY {
            chaperone_asked = (Instant::now(), chaperone_asked.1.wrapping_add(1));
            shared.values.lock().publish(host_value::CHAPERONE_REQUEST, chaperone_asked.1.to_le_bytes().to_vec(), false);
        }
        shared.send_values();
        let Ok((length, from)) = shared.socket.recv_from(&mut buffer) else { continue };
        let datagram = &buffer[..length];
        if from.ip() != headset_ip {
            continue;
        }
        if let Some(answer) = link::probe_answer(datagram) {
            shared.socket.send_to(&answer, from).ok();
            continue;
        }
        if link::is_probe(datagram) {
            continue;
        }
        let Some(elements) = link::unpack(shared.session_id, datagram) else { continue };
        last_heard = Instant::now();
        // The headset sends from the port it listens on; answers go there.
        if shared.headset.lock().replace(from).is_none() {
            info!("Steam Link: the headset's stream arrived from {from}");
        }
        for element in elements {
            match (element.class, element.kind) {
                (class::UNRELIABLE, unreliable::POSE) if element.value.len() >= POSE_BLOCK => {
                    let pose = &element.value;
                    shared.clock.lock().update(i32_at(pose, 0x08), i32_at(pose, 0x04), now_us());
                    let reset = u16::from_le_bytes([pose[0x18], pose[0x19]]);
                    if reset_counter.is_some_and(|r| r != reset) {
                        // The headset lost a frame it couldn't recover and reset its decoder: a
                        // keyframe, and less to send for a while.
                        control.idr_requested.store(true, Ordering::Relaxed);
                        shared.latency.lock().resets += 1;
                        if let Some(bps) = shared.rate.lock().loss(true, Instant::now()) {
                            info!("Steam Link: the headset reset its decoder; bitrate down to {:.0} Mbps", bps as f64 / 1e6);
                        }
                    }
                    reset_counter = Some(reset);
                    // Carried to when the frame drawn from it will be seen: the lead the headset's reports measure.
                    let arrived = now_us();
                    let lead = shared.latency.lock().lead;
                    let ahead = lead / 1e6;
                    let head = packed_pose(&pose[0x60..0x60 + PACKED_POSE]);
                    let (orientation, position) = predict(head.orientation, head.position, head.linear, head.angular, ahead);
                    tracking_ns += 1;
                    {
                        let mut state = control.host_state.lock();
                        state.tracking_timestamp_ns = tracking_ns;
                        state.head_orientation = orientation;
                        state.head_position = position;
                        state.linear_velocity = head.linear;
                        state.angular_velocity = head.angular;
                        state.has_pose = true;
                    }
                    control.host_state_dirty.store(true, Ordering::Release);
                    {
                        let mut rendered = shared.rendered.lock();
                        rendered.push_back(Rendered { tracking_ns, orientation, position, arrived, display_time: arrived.wrapping_add(lead as i32), fov: None });
                        while rendered.len() > 256 {
                            rendered.pop_front();
                        }
                    }
                    if controllers {
                        let mut hands = control.input_state.lock();
                        for (hand, state) in hands.iter_mut().enumerate() {
                            let device = packed_pose(&pose[0xb8 + hand * PACKED_POSE..0xb8 + (hand + 1) * PACKED_POSE]);
                            let (orientation, position) = predict(device.orientation, device.position, device.linear, device.angular, ahead);
                            *state = ControllerState {
                                active: device.valid,
                                orientation,
                                position,
                                linear_velocity: device.linear,
                                angular_velocity: device.angular,
                                inputs: touch_inputs(&pose[0x28 + hand * 28..0x28 + (hand + 1) * 28], hand == 1),
                            };
                        }
                        control.input_state_dirty.store(true, Ordering::Release);
                    }
                    let completed = shared.values.lock().receive(&pose[POSE_BLOCK..]);
                    headset_values(&shared, &completed, &control, fps, &mut rate_requested, &mut optics, &mut seated, &data, panel);
                }
                (class::UNRELIABLE, unreliable::STATISTICS) => statistics(&shared, &element.value),
                (class::VIDEO_REPLY, video_kind::FRAME_REPORT) if element.value.len() >= 24 => frame_report(&shared, &element),
                (class::UNRELIABLE, unreliable::GOODBYE) => {
                    info!("Steam Link: the headset said goodbye");
                    shared.ended.store(true, Ordering::Relaxed);
                }
                (class::VIDEO_REPLY, video_kind::RETRANSMIT_REQUEST) => {
                    let ids: Vec<u16> = element.value.chunks_exact(2).map(|c| u16::from_le_bytes([c[0], c[1]])).collect();
                    shared.latency.lock().retransmits += 1;
                    let elements = shared.video.lock().resend(element.id, &ids);
                    for datagram in video::datagrams(elements) {
                        shared.send(&datagram);
                    }
                }
                _ => {}
            }
        }
    }
    shared.ended.store(true, Ordering::Relaxed);
}

/// Values the headset finished sending.
fn headset_values(
    shared: &Shared,
    completed: &[u8],
    control: &StreamControl,
    fps: f32,
    rate_requested: &mut bool,
    optics: &mut (Option<[[f32; 4]; 2]>, Option<f32>),
    seated: &mut Option<Seated>,
    data: &std::path::Path,
    panel: Option<(u32, u32)>,
) {
    for &id in completed {
        let Some(value) = shared.values.lock().remote.get(&id).cloned() else { continue };
        match id {
            headset_value::PROJECTION if value.len() >= 32 => {
                let tangents: [[f32; 4]; 2] = std::array::from_fn(|eye| std::array::from_fn(|i| f32_at(&value, eye * 16 + i * 4)));
                *shared.tangents.lock() = tangents;
                optics.0 = Some(tangents);
            }
            headset_value::EYE_TO_HEAD if value.len() >= 96 => {
                // Two row-major 3x4 matrices; each eye's x is its fourth float.
                optics.1 = Some(f32_at(&value, 48 + 12) - f32_at(&value, 12));
            }
            headset_value::RATES if value.len() >= 16 => {
                let count = (u32::from_le_bytes(value[4..8].try_into().unwrap()) as usize).min((value.len() - 16) / 4);
                let rates: Vec<f32> = (0..count).map(|i| f32_at(&value, 16 + i * 4)).collect();
                // The headset's rate nearest the game's pace.
                if let Some(&rate) = rates.iter().min_by(|a, b| (*a - fps).abs().total_cmp(&(*b - fps).abs())) {
                    info!("Steam Link: the headset offers {rates:?} Hz; asking for {rate}");
                    shared.values.lock().publish(host_value::RATE_REQUEST, rate.to_le_bytes().to_vec(), !*rate_requested);
                    *rate_requested = true;
                }
            }
            headset_value::FRAME_TIME if value.len() >= 4 => {
                let us = u32::from_le_bytes(value[..4].try_into().unwrap()).max(1);
                shared.latency.lock().period = us as f32;
                info!("Steam Link: the headset runs at {:.1} Hz", 1e6 / us as f64);
            }
            headset_value::VERSION => info!("Steam Link: the headset's app is {}", String::from_utf8_lossy(&value)),
            headset_value::CHAPERONE => {
                if let Some(now) = seated_pose(&value) {
                    if seated.is_none_or(|before| before != now) {
                        info!("Steam Link: the headset's seated pose is at {:?}, yaw {:.3}", now.translation, now.yaw);
                    }
                    if seated.is_some_and(|before| before.moved_from(&now)) {
                        // The player recentred (holding the headset's button): the game recentres too.
                        let mut state = control.host_state.lock();
                        state.recenter_generation += 1;
                        control.host_state_dirty.store(true, Ordering::Release);
                        info!("Steam Link: the player recentred (generation {})", state.recenter_generation);
                    }
                    *seated = Some(now);
                }
            }
            _ => {}
        }
    }
    if let (Some(tangents), Some(ipd)) = *optics {
        // The game's views: each eye's field of view (left, right, up, down angles) and the IPD.
        let mut state = control.host_state.lock();
        let fov = tangents.map(|[left, right, down, up]| [left.atan(), right.atan(), up.atan(), down.atan()]);
        if !state.has_views || state.fov != fov || state.ipd_m != ipd {
            info!("Steam Link: headset views: ipd {:.1} mm, tangents {tangents:?}", ipd * 1000.0);
            state.fov = fov;
            state.ipd_m = ipd;
            state.has_views = true;
            control.host_state_dirty.store(true, Ordering::Release);
            crate::shared_frames::save_headset_optics(data, &fov, ipd, panel);
        }
    }
}

/// The seated pose in the headset's chaperone_info JSON: where its play space's centre is, and which
/// way it faces, which a recentre moves.
#[derive(Clone, Copy, Debug, PartialEq)]
struct Seated {
    translation: [f64; 3],
    yaw: f64,
}

impl Seated {
    /// Moved further than the headset's own wobble from one report to the next.
    fn moved_from(&self, other: &Seated) -> bool {
        let distance = (0..3).map(|i| (self.translation[i] - other.translation[i]).powi(2)).sum::<f64>().sqrt();
        let turn = (self.yaw - other.yaw).sin().atan2((self.yaw - other.yaw).cos()).abs();
        distance > 0.05 || turn > 0.05
    }
}

fn seated_pose(value: &[u8]) -> Option<Seated> {
    let info: serde_json::Value = serde_json::from_slice(value.strip_suffix(&[0]).unwrap_or(value)).ok()?;
    let seated = info.get("universes")?.as_array()?.first()?.get("seated")?;
    let translation = seated.get("translation")?.as_array()?;
    let number = |v: &serde_json::Value| v.as_f64();
    Some(Seated {
        translation: [number(translation.first()?)?, number(translation.get(1)?)?, number(translation.get(2)?)?],
        yaw: seated.get("yaw")?.as_f64()?,
    })
}

/// How far the headset has got with the frames: complete off the network, decoded, and expected on
/// its display (its clock). Each stage goes to the log, and the lead follows pose-to-display.
fn frame_report(shared: &Shared, element: &Element) {
    let value = &element.value;
    let (decoded_id, accepted_id) = (element.id, element.index);
    let (decoded_at, accepted_at) = (i32_at(value, 0), i32_at(value, 4));
    let shown_id = u16::from_le_bytes([value[8], value[9]]);
    let shown_at = i32_at(value, 0x0c);
    let bitrate = shared.rate.lock().current();
    let clock = shared.clock.lock();
    let mut latency = shared.latency.lock();
    latency.reports += 1;
    if accepted_id != latency.last_accepted && accepted_at != 0 {
        latency.last_accepted = accepted_id;
        latency.accepted.push_back((accepted_id, accepted_at));
        while latency.accepted.len() > 128 {
            latency.accepted.pop_front();
        }
        if let Some((sent, _)) = latency.sent_frame(accepted_id) {
            let network = clock.to_host(accepted_at).wrapping_sub(sent);
            if (0..1_000_000).contains(&network) {
                latency.add(1, network);
            } else {
                latency.network_outside += 1;
                latency.network_raw = network;
            }
        }
    }
    if decoded_id != latency.last_decoded && decoded_at != 0 {
        latency.last_decoded = decoded_id;
        // Decoding, against when that same frame came off the network (both the headset's clock).
        if let Some(&(_, accepted)) = latency.accepted.iter().find(|(id, _)| *id == decoded_id) {
            latency.add(2, decoded_at.wrapping_sub(accepted));
        }
        if let Some((_, arrived)) = latency.sent_frame(decoded_id) {
            let target = clock.to_host(decoded_at).wrapping_sub(arrived) as f32 + latency.period;
            if (0.0..MOST_LEAD_US).contains(&target) {
                latency.lead += (target - latency.lead) * 0.1;
                latency.clock_leads += 1;
            }
        }
    }
    if shown_id != latency.last_shown && shown_at != 0 {
        latency.last_shown = shown_id;
        if let Some((_, arrived)) = latency.sent_frame(shown_id) {
            latency.add(3, clock.to_host(shown_at).wrapping_sub(arrived));
        }
    }
    latency.report(bitrate);
}

/// The headset's string -> double statistics (docs/svl-values/svlroot-key-value-channel.md):
/// three records of a 20-byte name, a double and a time. A pass rate under 99.99 % turns Q on and
/// lowers the bitrate; the ping helps predict poses.
fn statistics(shared: &Shared, value: &[u8]) {
    for record in value.chunks_exact(32) {
        let name_end = record[..20].iter().position(|&b| b == 0).unwrap_or(20);
        let number = f64::from_le_bytes(record[20..28].try_into().unwrap());
        match &record[..name_end] {
            b"cRXpass" => {
                shared.latency.lock().pass = Some(number);
                if number < 99.99 {
                    *shared.q_until.lock() = Instant::now() + Q_AFTER_LOSS;
                    if let Some(bps) = shared.rate.lock().loss(false, Instant::now()) {
                        info!("Steam Link: the headset received {number:.2} % of packets; bitrate down to {:.0} Mbps", bps as f64 / 1e6);
                    }
                }
            }
            b"cPingMS" if number > 0.0 && number < 1000.0 => shared.latency.lock().ping_us = Some((number * 1000.0) as f32),
            _ => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_headsets_clock_converts_whatever_the_offset() {
        // The headset's clock 1.5e9 us behind this host's, both ways round: over a quarter of the
        // 32-bit range, where halving a wrapped difference went half the range wrong.
        for offset in [1_500_000_000i32, -1_500_000_000, 1_000, 0x7fff_0000] {
            let clock = Clock { offset, confidence: 100, peer_offset: offset.wrapping_neg() };
            let headset_time = 123_456i32;
            assert_eq!(clock.to_host(headset_time), headset_time.wrapping_add(offset), "offset {offset}");
        }
    }

    #[test]
    fn the_clock_settles_without_overflowing() {
        // A first sample, then one 50 ms later on both clocks with 2 ms more delay: the estimate
        // moves a little towards it (at most 100 us a step), never by a wrapped amount.
        let mut clock = Clock::default();
        clock.update(1_000_000, 0, 5_000_000);
        let first = clock.offset;
        clock.update(1_050_000, 0, 5_048_000);
        assert!((clock.offset - first).abs() <= 100, "{} then {}", first, clock.offset);
        clock.update(1_100_000, 0, 5_101_000);
        assert!((clock.offset - first).abs() <= 105, "{} then {}", first, clock.offset);
    }

    #[test]
    fn a_recentre_moves_the_seated_pose() {
        let first = br#"{"jsonid":"chaperone_info","universes":[{"seated":{"translation":[0,0,0],"yaw":0},"standing":{"translation":[0,0,0],"yaw":0},"universeID":"1"}],"version":5}"#;
        let moved = br#"{"jsonid":"chaperone_info","universes":[{"seated":{"translation":[0.1,0,0.2],"yaw":1.2},"standing":{"translation":[0,0,0],"yaw":0},"universeID":"1"}],"version":5}"#;
        let mut with_nul = first.to_vec();
        with_nul.push(0);
        assert_eq!(seated_pose(&with_nul), seated_pose(first));
        let (first, moved) = (seated_pose(first).unwrap(), seated_pose(moved).unwrap());
        assert!(first.moved_from(&moved));
        let wobble = Seated { translation: [0.01, 0.0, 0.0], yaw: 0.01 };
        assert!(!first.moved_from(&wobble));
        assert_eq!(seated_pose(b"not json"), None);
    }

    #[test]
    fn panels_by_name() {
        assert_eq!(panel_size("Oculus Quest 3"), Some((2080, 2208)));
        assert_eq!(panel_size("Meta Quest 3S"), Some((1856, 1920)));
        assert_eq!(panel_size("Quest 2"), Some((1856, 1920)));
        assert_eq!(panel_size("Some Headset"), None);
    }

    #[test]
    fn prediction_turns_about_the_device_axes() {
        // A quarter turn a second about y, for a second: a quarter turn.
        let (q, p) = predict([0.0, 0.0, 0.0, 1.0], [0.0, 1.6, 0.0], [1.0, 0.0, 0.0], [0.0, std::f32::consts::FRAC_PI_2, 0.0], 1.0);
        assert!((q[1] - (std::f32::consts::FRAC_PI_4).sin()).abs() < 1e-5 && (q[3] - (std::f32::consts::FRAC_PI_4).cos()).abs() < 1e-5);
        assert_eq!(p, [1.0, 1.6, 0.0]);
        // Still: unchanged.
        assert_eq!(predict([0.0, 0.0, 0.0, 1.0], [1.0; 3], [0.0; 3], [0.0; 3], 0.05).0, [0.0, 0.0, 0.0, 1.0]);
    }

    #[test]
    fn touch_inputs_from_the_blocks() {
        let mut block = [0u8; 28];
        block[0] = 0b1011; // lower, upper, stick click (menu bit 2 off)
        block[8] = 0b10100; // trigger touch, thumbrest
        block[0x11] = 127; // trigger full
        block[0x12] = (-127i8) as u8; // stick full left
        block[0x14] = 64; // squeeze half
        let right = touch_inputs(&block, true);
        assert_eq!((right[input::A_CLICK], right[input::B_CLICK], right[input::THUMBSTICK_CLICK]), (1.0, 1.0, 1.0));
        assert_eq!((right[input::TRIGGER_TOUCH], right[input::THUMBREST_TOUCH], right[input::A_TOUCH]), (1.0, 1.0, 0.0));
        assert_eq!((right[input::TRIGGER_VALUE], right[input::THUMBSTICK_X]), (1.0, -1.0));
        assert!((right[input::SQUEEZE_VALUE] - 0.504).abs() < 0.01);
        block[0] = 0b0101; // lower and menu
        let left = touch_inputs(&block, false);
        assert_eq!((left[input::X_CLICK], left[input::MENU_CLICK], left[input::A_CLICK]), (1.0, 1.0, 0.0));
    }

    #[test]
    fn the_clock_converts_both_ways() {
        // Host clock 1,000,000 us ahead of the headset's, 2 ms each way.
        let mut clock = Clock::default();
        for i in 0..50 {
            let headset_sent = i * 10_000;
            clock.update(headset_sent, -1_000_000 + 2_000, headset_sent + 1_000_000 + 2_000);
        }
        assert_eq!(clock.to_host(5_000), 1_005_000);
    }

    #[test]
    fn the_packed_pose_layout() {
        let mut bytes = [0u8; PACKED_POSE];
        for (i, v) in [0.1f32, 1.6, -0.2, 0.9, 0.1, 0.2, 0.3].iter().enumerate() {
            bytes[i * 4..i * 4 + 4].copy_from_slice(&v.to_le_bytes());
        }
        bytes[0x36] = 2;
        let pose = packed_pose(&bytes);
        assert_eq!(pose.position, [0.1, 1.6, -0.2]);
        assert_eq!(pose.orientation, [0.1, 0.2, 0.3, 0.9]);
        assert!(pose.valid);
    }
}
