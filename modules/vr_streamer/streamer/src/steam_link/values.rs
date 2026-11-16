//! The value transport (OpenVRLink's docs/svl-values/value-transport-wire-format.md): each side
//! publishes values by id in 128-byte chunks and repeats each chunk until the other acknowledges
//! it. A data chunk is `0x80 | version, id, u16 chunk, u32 total` then up to 128 bytes; an
//! acknowledgement is `version, id, u16 chunk`.

use std::collections::{BTreeMap, BTreeSet};

const CHUNK: usize = 128;
const MOST: usize = 0x80_0000;

struct Outgoing {
    data: Vec<u8>,
    version: u8,
    unacked: BTreeSet<usize>,
}

struct Incoming {
    version: u8,
    data: Vec<u8>,
    missing: BTreeSet<usize>,
}

#[derive(Default)]
pub struct Values {
    local: BTreeMap<u8, Outgoing>,
    staged: BTreeMap<u8, Vec<u8>>,
    acks_owed: Vec<(u8, u8, u16)>,
    in_progress: BTreeMap<u8, Incoming>,
    /// The headset's values, complete.
    pub remote: BTreeMap<u8, Vec<u8>>,
    remote_versions: BTreeMap<u8, u8>,
}

fn chunks(size: usize) -> usize {
    size.div_ceil(CHUNK).max(1)
}

impl Values {
    /// Sets one of this side's values; an unchanged one isn't sent again unless forced.
    pub fn publish(&mut self, id: u8, data: Vec<u8>, force: bool) {
        if !force && !self.staged.contains_key(&id) && self.local.get(&id).is_some_and(|o| o.data == data) {
            return;
        }
        self.staged.insert(id, data);
    }

    /// Whether anything waits to be sent.
    pub fn pending(&self) -> bool {
        !self.acks_owed.is_empty() || !self.staged.is_empty() || self.local.values().any(|o| !o.unacked.is_empty())
    }

    /// Acknowledgements first, then every unacknowledged chunk that fits in `budget` bytes.
    pub fn tick(&mut self, budget: usize) -> Vec<u8> {
        let staged: Vec<u8> = self.staged.keys().copied().collect();
        for id in staged {
            if self.local.get(&id).is_some_and(|o| !o.unacked.is_empty()) {
                continue; // the previous value is still on its way
            }
            let data = self.staged.remove(&id).unwrap();
            let version = self.local.get(&id).map(|o| (o.version + 1) & 0x7f).unwrap_or(1).max(1);
            let unacked = (0..chunks(data.len())).collect();
            self.local.insert(id, Outgoing { data, version, unacked });
        }
        let mut out = Vec::new();
        while let Some(&(version, id, chunk)) = self.acks_owed.first() {
            if out.len() + 4 > budget {
                return out;
            }
            out.extend_from_slice(&[version, id]);
            out.extend_from_slice(&chunk.to_le_bytes());
            self.acks_owed.remove(0);
        }
        for (&id, outgoing) in &self.local {
            for &chunk in &outgoing.unacked {
                let start = chunk * CHUNK;
                let piece = &outgoing.data[start..(start + CHUNK).min(outgoing.data.len())];
                if out.len() + 8 + piece.len() > budget {
                    return out;
                }
                out.extend_from_slice(&[0x80 | outgoing.version, id]);
                out.extend_from_slice(&(chunk as u16).to_le_bytes());
                out.extend_from_slice(&(outgoing.data.len() as u32).to_le_bytes());
                out.extend_from_slice(piece);
            }
        }
        out
    }

    /// Reads the other side's bytes; returns the ids whose value completed.
    pub fn receive(&mut self, buffer: &[u8]) -> Vec<u8> {
        let mut completed = Vec::new();
        let mut offset = 0;
        while offset + 4 <= buffer.len() {
            let (first, id) = (buffer[offset], buffer[offset + 1]);
            let chunk = u16::from_le_bytes([buffer[offset + 2], buffer[offset + 3]]) as usize;
            let version = first & 0x7f;
            if first & 0x80 == 0 {
                if let Some(outgoing) = self.local.get_mut(&id).filter(|o| o.version == version) {
                    outgoing.unacked.remove(&chunk);
                }
                offset += 4;
                continue;
            }
            if offset + 8 > buffer.len() {
                break;
            }
            let total = u32::from_le_bytes(buffer[offset + 4..offset + 8].try_into().unwrap()) as usize;
            if total > MOST || total < chunk * CHUNK {
                break;
            }
            let size = CHUNK.min(total - chunk * CHUNK);
            if offset + 8 + size > buffer.len() {
                break;
            }
            let piece = &buffer[offset + 8..offset + 8 + size];
            offset += 8 + size;
            // Every data chunk is acknowledged, repeats and unknown ids too, or it comes forever.
            self.acks_owed.push((version, id, chunk as u16));
            if !self.in_progress.contains_key(&id) && self.remote_versions.get(&id) == Some(&version) {
                continue; // a repeat of a value already whole
            }
            let incoming = self
                .in_progress
                .entry(id)
                .and_modify(|i| {
                    if i.version != version {
                        *i = Incoming { version, data: vec![0; total], missing: (0..chunks(total)).collect() };
                    }
                })
                .or_insert_with(|| Incoming { version, data: vec![0; total], missing: (0..chunks(total)).collect() });
            if incoming.missing.remove(&chunk) {
                incoming.data[chunk * CHUNK..chunk * CHUNK + size].copy_from_slice(piece);
            }
            if incoming.missing.is_empty() {
                let incoming = self.in_progress.remove(&id).unwrap();
                self.remote.insert(id, incoming.data);
                self.remote_versions.insert(id, incoming.version);
                completed.push(id);
            }
        }
        completed
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_value_crosses_in_chunks_and_is_acknowledged() {
        let (mut host, mut headset) = (Values::default(), Values::default());
        let big: Vec<u8> = (0..300u32).map(|i| i as u8).collect();
        host.publish(0x21, 120f32.to_le_bytes().to_vec(), false);
        host.publish(0x26, big.clone(), false);
        // A small budget splits it over several messages.
        let mut completed = Vec::new();
        for _ in 0..10 {
            completed.extend(headset.receive(&host.tick(200)));
            host.receive(&headset.tick(200));
        }
        assert_eq!(completed, vec![0x21, 0x26]);
        assert_eq!(headset.remote[&0x26], big);
        assert!(!host.pending() && !headset.pending());
        // An unchanged value isn't sent again; a changed one is, as a new version.
        host.publish(0x21, 120f32.to_le_bytes().to_vec(), false);
        assert!(!host.pending());
        host.publish(0x21, 90f32.to_le_bytes().to_vec(), false);
        assert_eq!(headset.receive(&host.tick(400)), vec![0x21]);
        assert_eq!(headset.remote[&0x21], 90f32.to_le_bytes());
    }

    #[test]
    fn the_captured_rate_request() {
        // docs/svl-values: the headset's 0x0c reply, 8333 us, a single chunk.
        let mut headset = Values::default();
        headset.publish(0x0c, 8333u32.to_le_bytes().to_vec(), false);
        let bytes = headset.tick(372);
        assert_eq!(bytes, [0x81, 0x0c, 0, 0, 4, 0, 0, 0, 0x8d, 0x20, 0, 0]);
        let mut host = Values::default();
        assert_eq!(host.receive(&bytes), vec![0x0c]);
        assert_eq!(host.tick(372), [0x01, 0x0c, 0, 0]);
    }
}
