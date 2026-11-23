//! vr_streamer: streams the game to a headset's Steam Link app (steam_link/), from the frames the
//! game publishes (Purr's OpenXR runtime, modules/vr_streamer/runtime) through a VideoToolbox
//! encoder. The game starts it beside the game while VR runs, and again after each headset
//! session ends.
//!
//! Data flow: game -> shared memory -> stream::run -> Encoder -> Steam Link session.
//!
//! `vr_streamer --frames <shared file> --data <folder>`, plus the options in options.rs. Its log
//! is <folder>/streamer.log (the one before it, streamer_previous.log); its state goes to stdout
//! as JSON lines (status.rs), and it takes commands on stdin.

#[cfg(not(target_os = "macos"))]
compile_error!("the streamer runs on macOS: elsewhere, players use their own OpenXR runtime");

mod audio;
mod encoder;
mod logger;
mod options;
mod shared_frames;
mod status;
mod steam_link;
mod stream;
mod vision_pro;

use log::{error, info, warn};
use shared_frames::HostState;
use std::{
    io::BufRead,
    path::PathBuf,
    sync::{
        atomic::{AtomicBool, AtomicU64, Ordering},
        Arc,
    },
    thread::{self, JoinHandle},
    time::{Duration, Instant},
};
use stream::StreamControl;

/// Set by SIGTERM/SIGINT, or when stdin closes. The game stops the streamer this way when VR
/// stops; if the game dies, its end of our stdin closes. A clean exit lets the headset go and
/// releases the audio tap. Lines on stdin are commands: `pin <digits>` or `pin cancel`, the
/// player's answer to a `pin_needed` status line.
static STOP_REQUESTED: AtomicBool = AtomicBool::new(false);

/// How long a clean exit may take before the streamer leaves regardless.
/// The game waits a little longer (streamer_process.cpp) before killing it.
const SHUTDOWN_TIME: Duration = Duration::from_secs(2);

/// How often the host tries again for its ports when none was free.
const HOST_RETRY: Duration = Duration::from_secs(5);

extern "C" fn on_stop_signal(_signal: libc::c_int) {
    STOP_REQUESTED.store(true, Ordering::Relaxed);
}

/// A panic is logged and ends the process, so the game starts a fresh streamer.
fn install_panic_hook() {
    std::panic::set_hook(Box::new(|info| {
        let location = info.location().map(|l| format!("{}:{}", l.file(), l.line())).unwrap_or_default();
        let message = info
            .payload()
            .downcast_ref::<&str>()
            .map(|s| s.to_string())
            .or_else(|| info.payload().downcast_ref::<String>().cloned())
            .unwrap_or_default();
        error!("streamer panicked at {location}: {message}; exiting so the game restarts it");
        std::process::exit(101);
    }));
}

/// Takes frames for VR_STREAMER_ENCODE_TEST and sends them nowhere.
struct NoHeadset {
    eye: (u32, u32),
}

impl stream::Headset for NoHeadset {
    fn picture(&self) -> (u32, u32) {
        (self.eye.0, self.eye.1 * 2)
    }

    fn send_frame(&self, _frame: encoder::EncodedFrame) -> bool {
        true
    }

    fn send_haptics(&self, _hand: usize, _pulse: &shared_frames::HapticPulse) {}
}

fn main() {
    unsafe {
        libc::signal(libc::SIGTERM, on_stop_signal as *const () as usize);
        libc::signal(libc::SIGINT, on_stop_signal as *const () as usize);
    }

    let (commands_tx, commands) = std::sync::mpsc::channel::<String>();
    thread::spawn(move || {
        for line in std::io::stdin().lock().lines() {
            let Ok(line) = line else { break };
            commands_tx.send(line.trim().to_owned()).ok();
        }
        STOP_REQUESTED.store(true, Ordering::Relaxed);
    });

    let options = options::parse_options(std::env::args().skip(1));
    let Some(frames_path) = options.frames.clone() else {
        eprintln!("usage: vr_streamer --frames <shared file> --data <folder>");
        std::process::exit(2);
    };
    let data = options.data.clone().unwrap_or_else(|| PathBuf::from("."));
    logger::start(&data);
    install_panic_hook();
    let (eye, fps, bitrate_bps) = (options.eye, options.fps, options.bitrate_mbps * 1_000_000);
    info!("vr_streamer {}: {}x{} per eye at {fps} fps, {} Mbps", env!("CARGO_PKG_VERSION"), eye.0, eye.1, options.bitrate_mbps);

    let control = Arc::new(StreamControl {
        running: AtomicBool::new(false),
        latest_tracking_ns: AtomicU64::new(0),
        idr_requested: AtomicBool::new(true),
        host_state: parking_lot::Mutex::new(HostState::default()),
        host_state_dirty: AtomicBool::new(false),
        input_state: parking_lot::Mutex::new(Default::default()),
        input_state_dirty: AtomicBool::new(false),
    });

    // VR_STREAMER_ENCODE_TEST=<seconds> runs the frame pump without a headset: frames from the
    // game go through the encoder and, with VR_STREAMER_RECORD, to disk. Nothing is sent anywhere.
    if let Some(seconds) = std::env::var("VR_STREAMER_ENCODE_TEST").ok().and_then(|s| s.parse::<u64>().ok()) {
        info!("encode test for {seconds} s");
        control.running.store(true, Ordering::Relaxed);
        let handle = thread::spawn({
            let control = Arc::clone(&control);
            let frames_path = frames_path.clone();
            move || stream::run(Arc::new(NoHeadset { eye }), control, &frames_path, eye, fps, bitrate_bps)
        });
        let until = Instant::now() + Duration::from_secs(seconds);
        while Instant::now() < until && !STOP_REQUESTED.load(Ordering::Relaxed) {
            thread::sleep(Duration::from_millis(50));
        }
        control.running.store(false, Ordering::Relaxed);
        handle.join().ok();
        return;
    }

    // The game's sound comes from our own tap (audio.rs), made before a headset connects. Failure
    // is loud but not fatal: video still streams without sound.
    let game_audio = match audio::GameAudioCapture::start(options.mute_mac, options.game) {
        Ok(capture) => Some(capture),
        Err(e) => {
            error!("game audio unavailable: {e}");
            None
        }
    };
    if let Some(seconds) = std::env::var("VR_STREAMER_AUDIO_TEST").ok().and_then(|s| s.parse::<u64>().ok()) {
        if let Err(e) = audio::self_test(seconds) {
            error!("audio self-test: {e}");
        }
        return;
    }

    let mut host: Option<steam_link::host::Host> = None;
    let mut host_tried: Option<Instant> = None;
    let mut session: Option<Arc<steam_link::session::Session>> = None;
    let mut pump: Option<JoinHandle<()>> = None;
    info!("waiting for a headset (game audio {})", if game_audio.is_some() { "on" } else { "off" });
    status::status("waiting", serde_json::json!({ "audio": game_audio.is_some() }));
    vision_pro::watch();

    while !STOP_REQUESTED.load(Ordering::Relaxed) {
        if host.is_none() && host_tried.is_none_or(|at| at.elapsed() >= HOST_RETRY) {
            host = steam_link::host::start(steam_link::devices::shared_file());
            if host.is_none() && host_tried.is_none() {
                warn!("Steam Link: no free port to announce this Mac on; trying again every {HOST_RETRY:?}");
            }
            host_tried = Some(Instant::now());
        }
        while let Ok(command) = commands.try_recv() {
            if let (Some(host), Some(pin)) = (&host, command.strip_prefix("pin ")) {
                host.answer_pin(if pin == "cancel" {
                    steam_link::host::PinAnswer::Cancel
                } else {
                    steam_link::host::PinAnswer::Pin(pin.to_owned())
                });
            }
        }
        if let (Some(host), None) = (&host, &session) {
            if let Ok(start) = host.sessions.recv_timeout(Duration::from_millis(100)) {
                info!("Steam Link: {} connected, streaming HEVC at {fps} fps", start.device_name);
                status::status("connected", serde_json::json!({ "fps": fps }));
                let started = steam_link::session::Session::start(start, Arc::clone(&control), eye, fps, bitrate_bps, options.controllers, data.clone());
                steam_link::session::send_game_audio(Arc::clone(&started));
                control.running.store(true, Ordering::Relaxed);
                control.idr_requested.store(true, Ordering::Relaxed);
                let control = Arc::clone(&control);
                let frames_path = frames_path.clone();
                let headset: Arc<dyn stream::Headset> = started.clone();
                pump = Some(thread::spawn(move || stream::run(headset, control, &frames_path, eye, fps, bitrate_bps)));
                session = Some(started);
            }
            continue;
        }
        if session.as_ref().is_some_and(|s| s.ended()) {
            // One headset session per streamer: the game starts a fresh one, which frees
            // everything this one held.
            info!("Steam Link: the headset left; exiting so the next session starts clean");
            break;
        }
        thread::sleep(Duration::from_millis(100));
    }
    if STOP_REQUESTED.load(Ordering::Relaxed) {
        info!("stop requested");
    }

    // Leaving, however it goes: a streamer left running after the game kept the game in the Dock,
    // "running in the background".
    thread::spawn(|| {
        thread::sleep(SHUTDOWN_TIME);
        warn!("shutdown took over {SHUTDOWN_TIME:?}; leaving anyway");
        std::process::exit(0);
    });
    control.running.store(false, Ordering::Relaxed);
    if let Some(handle) = pump.take() {
        handle.join().ok();
    }
    // Dropping the capture destroys the tap and unmutes the Mac.
    drop(session);
    drop(game_audio);
}
