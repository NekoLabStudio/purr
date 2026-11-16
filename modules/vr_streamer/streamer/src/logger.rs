//! The streamer's log: <data>/streamer.log, the previous run's kept as streamer_previous.log
//! (each headset session ends the streamer, and the game starts another).

use std::{
    fs::File,
    io::Write,
    path::Path,
    sync::Mutex,
    time::{Instant, SystemTime, UNIX_EPOCH},
};

struct FileLogger {
    file: Mutex<File>,
    started: Instant,
}

impl log::Log for FileLogger {
    fn enabled(&self, metadata: &log::Metadata) -> bool {
        metadata.level() <= log::Level::Info
    }

    fn log(&self, record: &log::Record) {
        if self.enabled(record.metadata()) {
            let seconds = self.started.elapsed().as_secs_f64();
            if let Ok(mut file) = self.file.lock() {
                writeln!(file, "{seconds:9.3} {:5} {}", record.level(), record.args()).ok();
            }
        }
    }

    fn flush(&self) {
        if let Ok(mut file) = self.file.lock() {
            file.flush().ok();
        }
    }
}

/// Logs to `data`'s streamer.log from now on; on failure, nothing is logged.
pub fn start(data: &Path) {
    let path = data.join("streamer.log");
    std::fs::create_dir_all(data).ok();
    std::fs::rename(&path, data.join("streamer_previous.log")).ok();
    let Ok(mut file) = File::create(&path) else { return };
    let unix = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
    writeln!(file, "started at {unix} (Unix time); times below are seconds since").ok();
    if log::set_boxed_logger(Box::new(FileLogger { file: Mutex::new(file), started: Instant::now() })).is_ok() {
        log::set_max_level(log::LevelFilter::Info);
    }
}
