//! The stream's bitrate, lowered when the headset loses packets and raised again, a step at a
//! time, once it stops: a link that can't carry the player's choice for a while (a busy network,
//! the headset across the house) gets a softer picture rather than one that freezes.

use std::time::{Duration, Instant};

/// After this long without loss, the bitrate climbs back towards the player's choice.
const CALM: Duration = Duration::from_secs(3);
/// Changes come no closer together than this (a cut for heavy loss excepted).
const STEP: Duration = Duration::from_secs(1);
/// The least it goes to: a share of the player's choice, but at least 10 Mbps (or the choice).
const FLOOR_SHARE: f64 = 0.3;
const FLOOR_BPS: f64 = 10_000_000.0;

pub struct Rate {
    target: u64,
    current: u64,
    last_loss: Option<Instant>,
    last_change: Option<Instant>,
    last_heavy: Option<Instant>,
}

impl Rate {
    pub fn new(target_bps: u64) -> Self {
        Self { target: target_bps, current: target_bps, last_loss: None, last_change: None, last_heavy: None }
    }

    pub fn current(&self) -> u64 {
        self.current
    }

    pub fn is_at_target(&self) -> bool {
        self.current >= self.target
    }

    fn floor(&self) -> u64 {
        (self.target as f64 * FLOOR_SHARE).max(FLOOR_BPS).min(self.target as f64) as u64
    }

    fn set(&mut self, bps: u64, now: Instant) -> Option<u64> {
        let bps = bps.clamp(self.floor(), self.target);
        if bps == self.current {
            return None;
        }
        self.current = bps;
        self.last_change = Some(now);
        Some(bps)
    }

    /// The headset lost packets: heavily (its decoder reset) or lightly (a lower pass rate). The
    /// new bitrate, if it changed.
    pub fn loss(&mut self, heavy: bool, now: Instant) -> Option<u64> {
        self.last_loss = Some(now);
        if heavy {
            // One hiccup can reset the decoder several times in a row: one cut for all of them.
            if self.last_heavy.is_some_and(|at| now.duration_since(at) < STEP) {
                return None;
            }
            self.last_heavy = Some(now);
            return self.set((self.current as f64 * 0.75) as u64, now);
        }
        if self.last_change.is_some_and(|at| now.duration_since(at) < STEP) {
            return None;
        }
        self.set((self.current as f64 * 0.9) as u64, now)
    }

    /// Called now and then: the new bitrate, if it climbed back a step.
    pub fn tick(&mut self, now: Instant) -> Option<u64> {
        if self.current >= self.target || self.last_loss.is_some_and(|at| now.duration_since(at) < CALM) {
            return None;
        }
        if self.last_change.is_some_and(|at| now.duration_since(at) < STEP) {
            return None;
        }
        self.set((self.current as f64 * 1.05).ceil() as u64, now)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const MBPS: u64 = 1_000_000;

    #[test]
    fn loss_lowers_it_and_calm_brings_it_back() {
        let start = Instant::now();
        let mut rate = Rate::new(100 * MBPS);
        assert_eq!(rate.loss(true, start), Some(75 * MBPS));
        // Light loss right after a change waits its turn.
        assert_eq!(rate.loss(false, start + Duration::from_millis(500)), None);
        assert_eq!(rate.loss(false, start + Duration::from_millis(1600)), Some(67_500_000));
        // Not back up while the link is still losing.
        assert_eq!(rate.tick(start + Duration::from_secs(3)), None);
        // Calm: a step a second, to the player's choice and no further.
        let mut now = start + Duration::from_millis(1600) + CALM;
        let mut steps = 0;
        while rate.tick(now).is_some() {
            steps += 1;
            now += STEP;
        }
        assert_eq!(rate.current(), 100 * MBPS);
        assert!(steps > 5, "{steps}");
    }

    #[test]
    fn resets_in_a_row_cut_once() {
        let now = Instant::now();
        let mut rate = Rate::new(100 * MBPS);
        assert_eq!(rate.loss(true, now), Some(75 * MBPS));
        assert_eq!(rate.loss(true, now + Duration::from_millis(1)), None);
        assert_eq!(rate.loss(true, now + Duration::from_millis(100)), None);
        assert_eq!(rate.loss(true, now + STEP), Some(56_250_000));
    }

    #[test]
    fn it_never_goes_below_its_floor() {
        let mut now = Instant::now();
        let mut rate = Rate::new(100 * MBPS);
        for _ in 0..20 {
            rate.loss(true, now);
            now += STEP;
        }
        assert_eq!(rate.current(), 30 * MBPS);
        let mut low = Rate::new(15 * MBPS);
        for _ in 0..20 {
            low.loss(true, now);
            now += STEP;
        }
        assert_eq!(low.current(), 10 * MBPS);
        let mut tiny = Rate::new(5 * MBPS);
        assert_eq!(tiny.loss(true, now), None);
    }
}
