//! Sending a frame (OpenVRLink's docs/svl-video/): a descriptor ("bookend") saying what the frame
//! is, then its encoded bytes in 1024-byte packets, eight to a stripe with parity, 64 to a group,
//! each group followed by another bookend; as Steam's host sends them. The last 16 frames are kept
//! for packets the headset asks for again.

use super::{
    fec::{self, DATA_PER_STRIPE, PACKET},
    link::{class, video, Element, DATAGRAM_ROOM},
};
use std::collections::VecDeque;

/// The protocol version this host speaks: the one Steam's captured host sent.
pub const VERSION: u32 = 14;
/// Which error correction the packets carry: the only one there is.
const FEC_MODE: u8 = 0x20;
const DESCRIPTOR_SIZE: usize = 0x3f8;
pub const METADATA_SIZE: usize = 0xf0;
const TRAILER_VERSION: u8 = 3;
const DATA_PER_GROUP: usize = 64;
const FIRST_PARITY: u16 = 0x800;
const STRIPES_PER_GROUP: usize = DATA_PER_GROUP / DATA_PER_STRIPE;
/// The headset keeps 16 frames; so does the host, for resending.
const KEPT: usize = 16;
/// In a retransmit request, in place of a packet id: the descriptor.
const DESCRIPTOR_REQUEST: u16 = 0xffff;

/// What the headset needs to show a frame (`TransportFrameMetadata_t`, the short 0xf0 form).
pub struct Metadata {
    pub present_number: u32,
    /// The head pose the frame was drawn for: orientation (x, y, z, w) and position, in meters.
    pub orientation: [f32; 4],
    pub position: [f32; 3],
    /// Each eye's frustum as tangents: left, right, down, up (left and down negative).
    pub tangents: [[f32; 4]; 2],
    /// When the frame should be seen, on this host's clock.
    pub display_time: u32,
    pub atlas: (u16, u16),
    pub tile: (u16, u16),
}

impl Metadata {
    pub fn bytes(&self) -> [u8; METADATA_SIZE] {
        let mut out = [0u8; METADATA_SIZE];
        let mut put = |offset: usize, bytes: &[u8]| out[offset..offset + bytes.len()].copy_from_slice(bytes);
        put(0x00, &self.present_number.to_le_bytes());
        // The pose, a row-major 3x4 (rotation then translation per row), then 0 0 0 1.
        let [x, y, z, w] = self.orientation;
        let rotation = [
            [1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w), 2.0 * (x * z + y * w)],
            [2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w)],
            [2.0 * (x * z - y * w), 2.0 * (y * z + x * w), 1.0 - 2.0 * (x * x + y * y)],
        ];
        let mut matrix = Vec::with_capacity(16);
        for row in 0..3 {
            matrix.extend_from_slice(&rotation[row]);
            matrix.push(self.position[row]);
        }
        matrix.extend_from_slice(&[0.0, 0.0, 0.0, 1.0]);
        for (i, value) in matrix.iter().enumerate() {
            put(0x04 + i * 4, &value.to_le_bytes());
        }
        // Each eye's projection, an OpenVR-style row-major 4x4 the headset reads its field of view from.
        for (eye, &[left, right, down, up]) in self.tangents.iter().enumerate() {
            let (near, far) = (0.1f32, 1000.0f32);
            let projection = [
                [2.0 / (right - left), 0.0, (right + left) / (right - left), 0.0],
                [0.0, 2.0 / (up - down), (up + down) / (up - down), 0.0],
                [0.0, 0.0, far / (near - far), near * far / (near - far)],
                [0.0, 0.0, -1.0, 0.0],
            ];
            for (i, value) in projection.iter().flatten().enumerate() {
                put(0x44 + eye * 0x40 + i * 4, &value.to_le_bytes());
            }
        }
        // Constant in every capture; nothing in the headset reads them.
        put(0xc4, &[0x02, 0x02, 0xca, 0x00]);
        // No foveal insets: the atlas is the two eyes stacked.
        put(0xe0, &self.display_time.to_le_bytes());
        put(0xe5, &[0x01]);
        put(0xe6, &self.atlas.0.to_le_bytes());
        put(0xe8, &self.atlas.1.to_le_bytes());
        put(0xea, &self.tile.0.to_le_bytes());
        put(0xec, &self.tile.1.to_le_bytes());
        put(0xee, &[0x00, 0x02]);
        out
    }
}

struct SentFrame {
    id: u16,
    /// The encoded frame, zero-padded to whole stripes.
    data: Vec<u8>,
    size: usize,
    descriptor: Vec<u8>,
    q: bool,
}

impl SentFrame {
    fn packets(&self) -> usize {
        self.size.div_ceil(PACKET)
    }

    fn data_packet(&self, n: usize) -> Element {
        let end = ((n + 1) * PACKET).min(self.size);
        Element { class: class::VIDEO, kind: video::PACKET, id: self.id, index: n as u16, value: self.data[n * PACKET..end].to_vec() }
    }

    /// P (`q` false) or Q of a stripe.
    fn parity_packet(&self, stripe: usize, q: bool) -> Element {
        let packets: [&[u8]; DATA_PER_STRIPE] = std::array::from_fn(|j| {
            let start = (stripe * DATA_PER_STRIPE + j) * PACKET;
            &self.data[start..start + PACKET]
        });
        let (p_bytes, q_bytes) = fec::encode(&packets);
        let index = FIRST_PARITY + (2 * stripe + q as usize) as u16;
        Element { class: class::VIDEO, kind: video::PACKET, id: self.id, index, value: if q { q_bytes } else { p_bytes }.to_vec() }
    }

    fn bookend(&self, group: usize) -> Element {
        let mut value = self.descriptor.clone();
        value[0x14] = group.min(255) as u8;
        Element { class: class::VIDEO, kind: video::DESCRIPTOR, id: self.id, index: group as u16, value }
    }
}

#[derive(Default)]
pub struct VideoSender {
    sent: VecDeque<SentFrame>,
    next_id: u16,
}

/// Elements packed into datagrams of up to DATAGRAM_ROOM bytes, in order.
pub fn datagrams(elements: Vec<Element>) -> Vec<Vec<Element>> {
    let mut out: Vec<Vec<Element>> = Vec::new();
    let mut room = 0;
    for element in elements {
        if out.is_empty() || room + element.size() > DATAGRAM_ROOM {
            out.push(Vec::new());
            room = 0;
        }
        room += element.size();
        out.last_mut().unwrap().push(element);
    }
    out
}

impl VideoSender {
    /// A frame's elements in sending order. `tx_time` and `peer_clock` are this host's clock and
    /// its estimate of the headset's offset; `q`: also send Q parity (for a lossy link).
    pub fn frame(&mut self, encoded: &[u8], metadata: &[u8; METADATA_SIZE], tx_time: u32, peer_clock: i32, q: bool) -> Vec<Element> {
        let id = self.next_id;
        self.next_id = self.next_id.wrapping_add(1);
        let size = encoded.len();
        let mut data = encoded.to_vec();
        data.resize(size.div_ceil(PACKET * DATA_PER_STRIPE).max(1) * PACKET * DATA_PER_STRIPE, 0);

        let mut descriptor = vec![0u8; DESCRIPTOR_SIZE];
        descriptor[0x00..0x04].copy_from_slice(&tx_time.to_le_bytes());
        descriptor[0x04..0x08].copy_from_slice(&peer_clock.to_le_bytes());
        descriptor[0x08..0x0c].copy_from_slice(&(size as u32).to_le_bytes());
        descriptor[0x0c] = 1; // constant 1 in every capture
        descriptor[0x0d] = FEC_MODE;
        descriptor[0x10..0x14].copy_from_slice(&u32::MAX.to_le_bytes());
        descriptor[0x16..0x18].copy_from_slice(&(METADATA_SIZE as u16).to_le_bytes());
        descriptor[0x18..0x1c].copy_from_slice(&VERSION.to_le_bytes());
        descriptor[0x20..0x20 + METADATA_SIZE].copy_from_slice(metadata);
        // An empty trailer: no values, timing events or value-transport bytes ride here.
        descriptor[0x20 + METADATA_SIZE + 0x0c] = TRAILER_VERSION;

        let frame = SentFrame { id, data, size, descriptor, q };
        let packets = frame.packets();
        let groups = packets.div_ceil(DATA_PER_GROUP).max(1);
        let mut elements = vec![frame.bookend(0)];
        for group in 0..groups {
            for i in 0..DATA_PER_GROUP {
                // Column-major across the group's eight stripes: a burst of loss costs each stripe one packet.
                let n = group * DATA_PER_GROUP + i / DATA_PER_STRIPE + (i % DATA_PER_STRIPE) * DATA_PER_STRIPE;
                if n < packets {
                    elements.push(frame.data_packet(n));
                }
            }
            let stripes: Vec<usize> = (0..STRIPES_PER_GROUP)
                .map(|s| group * STRIPES_PER_GROUP + s)
                .take_while(|stripe| stripe * DATA_PER_STRIPE < packets)
                .collect();
            for &stripe in &stripes {
                elements.push(frame.parity_packet(stripe, false));
            }
            if q {
                for &stripe in &stripes {
                    elements.push(frame.parity_packet(stripe, true));
                }
            }
            elements.push(frame.bookend(group + 1));
        }
        self.sent.push_back(frame);
        while self.sent.len() > KEPT {
            self.sent.pop_front();
        }
        elements
    }

    /// The packets the headset asked for again, from the frames still kept.
    pub fn resend(&self, frame_id: u16, ids: &[u16]) -> Vec<Element> {
        let Some(frame) = self.sent.iter().find(|f| f.id == frame_id) else { return Vec::new() };
        let mut out = Vec::new();
        for &id in ids {
            if id == DESCRIPTOR_REQUEST {
                out.push(frame.bookend(0));
            } else if (id as usize) < frame.packets() {
                out.push(frame.data_packet(id as usize));
            } else if id >= FIRST_PARITY {
                let parity = (id - FIRST_PARITY) as usize;
                let (stripe, q) = (parity / 2, parity % 2 == 1);
                if stripe * DATA_PER_STRIPE < frame.packets() && (!q || frame.q) {
                    out.push(frame.parity_packet(stripe, q));
                }
            }
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn metadata() -> [u8; METADATA_SIZE] {
        Metadata {
            present_number: 7,
            orientation: [0.0, 0.0, 0.0, 1.0],
            position: [0.0, 1.6, 0.0],
            tangents: [[-1.0, 1.0, -1.0, 1.0]; 2],
            display_time: 123_456,
            atlas: (1536, 3072),
            tile: (1536, 1536),
        }
        .bytes()
    }

    #[test]
    fn metadata_matches_the_captured_layout() {
        let m = metadata();
        let f = |o: usize| f32::from_le_bytes(m[o..o + 4].try_into().unwrap());
        // Identity rotation and (0, 1.6, 0), with the 1.6 at +0x20 as captured.
        assert_eq!((f(0x04), f(0x18), f(0x2c), f(0x20)), (1.0, 1.0, 1.0, 1.6));
        // The captured projection for tangents of 1: [1 0 0 0 / 0 1 0 0 / 0 0 -1.0001 -0.10001 / 0 0 -1 0].
        assert_eq!((f(0x44), f(0x58), f(0x7c)), (1.0, 1.0, -1.0));
        assert!((f(0x6c) + 1.0001).abs() < 1e-4 && (f(0x70) + 0.10001).abs() < 1e-5);
        // The headset's reading of tangents from the matrix: left (m02 - 1) / m00 and up (m12 + 1) / m11.
        assert_eq!(((f(0x4c) - 1.0) / f(0x44), (f(0x5c) + 1.0) / f(0x58)), (-1.0, 1.0));
        assert_eq!(u16::from_le_bytes([m[0xe8], m[0xe9]]), 3072);
        assert_eq!(m[0xe5], 1);
    }

    #[test]
    fn a_frame_goes_in_steams_order() {
        // Frame 8 of the capture: 9,863 bytes, 10 data packets: B0 d0 d8 d1 d9 d2..d7 P0 P1 Q0 Q1 B1.
        let mut sender = VideoSender::default();
        let encoded: Vec<u8> = (0..9863u32).map(|i| i as u8).collect();
        let elements = sender.frame(&encoded, &metadata(), 1, 2, true);
        let names: Vec<String> = elements
            .iter()
            .map(|e| match (e.kind, e.index) {
                (video::DESCRIPTOR, g) => format!("B{g}"),
                (_, i) if i >= FIRST_PARITY => format!("{}{}", if (i - FIRST_PARITY) % 2 == 0 { "P" } else { "Q" }, (i - FIRST_PARITY) / 2),
                (_, i) => format!("d{i}"),
            })
            .collect();
        assert_eq!(names.join(" "), "B0 d0 d8 d1 d9 d2 d3 d4 d5 d6 d7 P0 P1 Q0 Q1 B1");
        // The short last packet ends the frame exactly; reassembled, it's the frame.
        let mut data: Vec<&Element> = elements.iter().filter(|e| e.kind == video::PACKET && e.index < FIRST_PARITY).collect();
        data.sort_by_key(|e| e.index);
        assert_eq!(data.iter().flat_map(|e| e.value.clone()).collect::<Vec<u8>>(), encoded);
        // The descriptor: 1016 bytes, the frame's size, version 14, the trailer's version.
        let descriptor = &elements[0].value;
        assert_eq!(descriptor.len(), 0x3f8);
        assert_eq!(u32::from_le_bytes(descriptor[8..12].try_into().unwrap()), 9863);
        assert_eq!(u32::from_le_bytes(descriptor[0x18..0x1c].try_into().unwrap()), 14);
        assert_eq!(descriptor[0x110 + 0x0c], 3);
        // Resending: a data packet, a parity packet and the descriptor.
        let again = sender.resend(0, &[3, 0x801, 0xffff, 900]);
        assert_eq!(again.iter().map(|e| e.index).collect::<Vec<_>>(), vec![3, 0x801, 0]);
        assert_eq!(again[0].value, elements.iter().find(|e| e.kind == video::PACKET && e.index == 3).unwrap().value);
    }

    #[test]
    fn a_big_frame_has_groups_and_packs_into_datagrams() {
        let mut sender = VideoSender::default();
        let elements = sender.frame(&vec![1u8; 200 * PACKET], &metadata(), 0, 0, false);
        // 200 packets: 4 groups, 25 stripes of P, 5 bookends.
        assert_eq!(elements.iter().filter(|e| e.kind == video::DESCRIPTOR).count(), 5);
        assert_eq!(elements.iter().filter(|e| e.index >= FIRST_PARITY).count(), 25);
        let packed = datagrams(elements);
        assert!(packed.iter().all(|d| d.iter().map(Element::size).sum::<usize>() <= DATAGRAM_ROOM));
        assert_eq!(sender.frame(&[1, 2, 3], &metadata(), 0, 0, false)[0].id, 1);
    }
}
