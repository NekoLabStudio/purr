//! The streamer's state for whoever started it, one JSON object per line on stdout (the log goes
//! to its log file, never here):
//! - `{"event":"waiting","audio":<bool>}` once it listens for a headset (audio: whether the
//!   game's sound can reach it);
//! - `{"event":"connected","fps":<number>}` when a headset connects;
//! - `{"event":"stats","fps":<number>,"mbps":<number>,"encode_ms":<number>}` about once a second
//!   while streaming;
//! - `{"event":"vision_pro","nearby":<bool>}` at the first look and whenever an Apple Vision Pro
//!   appears on the network or leaves it (vision_pro.rs); the game shows itself there.
//!
//! A Steam Link headset pairing (once, for every game: steam_link/devices.rs) asks for
//! the PIN it shows:
//! - `{"event":"pin_needed","device":<name>}`: ask the player, and answer on stdin with a line
//!   `pin <digits>`, or `pin cancel`;
//! - `{"event":"pin_wrong","device":<name>}`: that PIN didn't match; a `pin_needed` follows;
//! - `{"event":"paired","device":<name>}` or `{"event":"pin_cancelled"}`: the prompt can go.
//!
//! A failed write is ignored: nobody may be reading.

use std::io::Write;

pub fn status(event: &str, detail: serde_json::Value) {
    let mut line = serde_json::json!({ "event": event });
    if let (Some(line), serde_json::Value::Object(detail)) = (line.as_object_mut(), detail) {
        line.extend(detail);
    }
    let mut stdout = std::io::stdout().lock();
    writeln!(stdout, "{line}").ok();
    stdout.flush().ok();
}
