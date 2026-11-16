//! Spreads a frame's datagrams over part of the frame interval instead of sending them in one
//! burst. A whole frame at once (a keyframe is several times an ordinary frame) can overflow a
//! router's or the headset's buffers on a good network, and the frame is lost: blocks, then a wait
//! for the next keyframe.

use std::time::Duration;

/// Sent together, then a pause: a dozen datagrams or so.
pub const BURST_BYTES: usize = 16 * 1024;

/// How long a frame of `bytes` takes to go out: at three times the stream's rate, so an ordinary
/// frame takes a third of its interval, but never more than three quarters of the interval, so a
/// keyframe doesn't hold up the next frame.
pub fn spread(bytes: usize, bitrate_bps: u64, period: Duration) -> Duration {
    let at_rate = Duration::from_secs_f64(bytes as f64 * 8.0 / (3.0 * bitrate_bps.max(1) as f64));
    at_rate.min(period.mul_f64(0.75))
}

/// When each datagram of `sizes` bytes goes, from the frame's start: bursts of up to BURST_BYTES,
/// spaced evenly across `spread`.
pub fn schedule(sizes: &[usize], spread: Duration) -> Vec<Duration> {
    let total: usize = sizes.iter().sum();
    let bursts = total.div_ceil(BURST_BYTES).max(1) as u32;
    let gap = spread / bursts;
    let mut sent = 0;
    sizes
        .iter()
        .map(|&size| {
            let at = gap * (sent / BURST_BYTES) as u32;
            sent += size;
            at
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn an_ordinary_frame_takes_a_third_of_its_interval() {
        let period = Duration::from_secs_f64(1.0 / 72.0);
        let frame = (100_000_000 / 8 / 72) as usize; // 100 Mbps at 72 Hz
        let took = spread(frame, 100_000_000, period);
        assert!((took.as_secs_f64() - period.as_secs_f64() / 3.0).abs() < 1e-4, "{took:?}");
    }

    #[test]
    fn a_keyframe_never_takes_more_than_three_quarters() {
        let period = Duration::from_secs_f64(1.0 / 72.0);
        assert_eq!(spread(2_000_000, 100_000_000, period), period.mul_f64(0.75));
    }

    #[test]
    fn bursts_are_spread_evenly_and_end_before_the_spread() {
        let sizes = vec![1400; 100]; // 140 kB: nine bursts
        let spread = Duration::from_millis(9);
        let at = schedule(&sizes, spread);
        assert_eq!(at.len(), sizes.len());
        assert_eq!(at[0], Duration::ZERO);
        assert!(at.windows(2).all(|w| w[0] <= w[1]));
        assert!(*at.last().unwrap() < spread);
        // No burst holds more than BURST_BYTES and one datagram.
        let mut by_time = std::collections::BTreeMap::new();
        for (time, size) in at.iter().zip(&sizes) {
            *by_time.entry(*time).or_insert(0usize) += size;
        }
        assert_eq!(by_time.len(), 140_000usize.div_ceil(BURST_BYTES));
        assert!(by_time.values().all(|&bytes| bytes <= BURST_BYTES + 1400));
    }

    #[test]
    fn a_small_frame_goes_at_once() {
        assert_eq!(schedule(&[1000, 1000], Duration::from_millis(5)), vec![Duration::ZERO, Duration::ZERO]);
    }
}
