//! How the game starts the streamer: its arguments, and the player's choices kept beside them.

use std::path::{Path, PathBuf};

pub struct Options {
    /// The shared file the game publishes frames in.
    pub frames: Option<PathBuf>,
    /// Where the log and the headset's optics are kept.
    pub data: Option<PathBuf>,
    /// Eye size the game renders and we stream, so nothing is scaled between the game and the
    /// encoder. Multiples of 32.
    pub eye: (u32, u32),
    /// Frames a second, the game's pace.
    pub fps: f32,
    pub bitrate_mbps: u64,
    pub mute_mac: bool,
    /// Whether the headset's controllers reach the game.
    pub controllers: bool,
    /// The game's process, whose sound goes to the headset. Unset, the process that started this
    /// one: a program that runs the streamer itself (PURR_VR_STREAMER=external) passes the game's.
    pub game: Option<i32>,
}

impl Default for Options {
    fn default() -> Self {
        Self { frames: None, data: None, eye: (2080, 2240), fps: 72.0, bitrate_mbps: 100, mute_mac: true, controllers: true, game: None }
    }
}

/// The player's choices from the game's settings, kept in the data folder for each start:
/// `{"bitrate": <Mbps>, "mute": <bool>}`, either of them, checked as the arguments are.
pub const PLAYER_CHOICES: &str = "stream.json";

/// `--frames <file> --data <folder> --eye <W>x<H> --fps <n> --bitrate <Mbps> --mute 0|1
/// --controllers 0|1 --game <pid>`; all but `--frames` optional. The player's choices (PLAYER_CHOICES in the
/// data folder) win over the arguments.
pub fn parse_options(args: impl Iterator<Item = String>) -> Options {
    let mut options = Options::default();
    apply_arguments(&mut options, args);
    if let Some(data) = options.data.clone() {
        apply_player_choices(&mut options, &data.join(PLAYER_CHOICES));
    }
    options
}

/// Applies the player's choices file, if there is one, as arguments.
fn apply_player_choices(options: &mut Options, path: &Path) {
    let Ok(text) = std::fs::read_to_string(path) else { return };
    let Ok(serde_json::Value::Object(choices)) = serde_json::from_str::<serde_json::Value>(&text) else { return };
    let mut args = Vec::new();
    if let Some(bitrate) = choices.get("bitrate").and_then(|v| v.as_f64()) {
        args.extend(["--bitrate".to_owned(), (bitrate.round() as u64).to_string()]);
    }
    if let Some(mute) = choices.get("mute").and_then(|v| v.as_bool()) {
        args.extend(["--mute".to_owned(), if mute { "1" } else { "0" }.to_owned()]);
    }
    apply_arguments(options, args.into_iter());
}

fn apply_arguments(options: &mut Options, args: impl Iterator<Item = String>) {
    let mut args = args.peekable();
    while let Some(arg) = args.next() {
        let value = args.next();
        match (arg.as_str(), value.as_deref()) {
            ("--bitrate", Some(v)) => {
                if let Ok(mbps) = v.parse::<u64>() {
                    options.bitrate_mbps = mbps.clamp(5, 200);
                }
            }
            ("--mute", Some(v)) => options.mute_mac = v == "1",
            ("--controllers", Some(v)) => options.controllers = v == "1",
            ("--game", Some(v)) => options.game = v.parse::<i32>().ok().filter(|pid| *pid > 0),
            ("--frames", Some(v)) => options.frames = Some(PathBuf::from(v)),
            ("--data", Some(v)) => options.data = Some(PathBuf::from(v)),
            ("--eye", Some(v)) => {
                if let Some((w, h)) = v.split_once('x').and_then(|(w, h)| Some((w.parse::<u32>().ok()?, h.parse::<u32>().ok()?))) {
                    if (256..=8192).contains(&w) && (256..=8192).contains(&h) {
                        options.eye = (w / 32 * 32, h / 32 * 32);
                    }
                }
            }
            ("--fps", Some(v)) => {
                if let Ok(fps) = v.parse::<f32>() {
                    if [72.0, 80.0, 90.0, 120.0].contains(&fps) {
                        options.fps = fps;
                    }
                }
            }
            _ => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(args: &[&str]) -> Options {
        parse_options(args.iter().map(|a| a.to_string()))
    }

    #[test]
    fn defaults_when_no_arguments() {
        let options = parse(&[]);
        assert_eq!(options.bitrate_mbps, 100);
        assert!(options.mute_mac);
        assert!(options.controllers);
    }

    #[test]
    fn parses_every_option() {
        let options = parse(&["--frames", "/tmp/f.bin", "--data", "/tmp/d", "--eye", "1700x1800", "--fps", "90", "--bitrate", "80", "--mute", "0", "--controllers", "0", "--game", "4321"]);
        assert_eq!(options.eye, (1696, 1792));
        assert_eq!(options.game, Some(4321));
        assert_eq!(options.fps, 90.0);
        assert_eq!(options.frames, Some(PathBuf::from("/tmp/f.bin")));
        assert_eq!(options.data, Some(PathBuf::from("/tmp/d")));
        assert_eq!(options.bitrate_mbps, 80);
        assert!(!options.mute_mac);
        assert!(!options.controllers);
    }

    #[test]
    fn clamps_bitrate_and_ignores_junk() {
        let options = parse(&["--bitrate", "1", "--fps", "61", "--bogus"]);
        assert_eq!(options.bitrate_mbps, 5);
        assert_eq!(options.fps, 72.0);
        assert_eq!(parse(&["--bitrate", "999"]).bitrate_mbps, 200);
    }

    #[test]
    fn player_choices_win_and_are_checked() {
        let data = std::env::temp_dir().join(format!("vr_streamer_choices_{}", std::process::id()));
        std::fs::create_dir_all(&data).unwrap();
        std::fs::write(data.join(PLAYER_CHOICES), r#"{"bitrate": 999.0, "mute": false}"#).unwrap();
        let options = parse(&["--data", data.to_str().unwrap(), "--bitrate", "50"]);
        assert_eq!(options.bitrate_mbps, 200);
        assert!(!options.mute_mac);
        std::fs::write(data.join(PLAYER_CHOICES), "not json").unwrap();
        assert_eq!(parse(&["--data", data.to_str().unwrap(), "--bitrate", "50"]).bitrate_mbps, 50);
        std::fs::remove_dir_all(&data).unwrap();
    }
}
