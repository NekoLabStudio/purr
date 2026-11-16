//! The stream's datagrams (OpenVRLink's docs/svl-transport/): a sequence number, a zero, a word
//! that makes the XOR of every 32-bit word of the datagram and the session id come to zero, then
//! back-to-back elements, each an 8-byte header (class, type, id, length, index) and its value.
//! This host hands out `MID0` tokens, the form Steam uses: checked, not encrypted.

pub const HEADER: usize = 8;
pub const ELEMENT_HEADER: usize = 8;
/// The most an element's value may hold.
pub const MAX_VALUE: usize = 0x400;
/// Elements are packed into datagrams of up to this much, as Steam's host does.
pub const DATAGRAM_ROOM: usize = 1368;

/// Element classes and the types this host reads or writes.
pub mod class {
    pub const UNRELIABLE: u8 = 3;
    pub const VIDEO_REPLY: u8 = 4;
    pub const VIDEO: u8 = 5;
    pub const AUDIO: u8 = 6;
}

pub mod unreliable {
    pub const POSE: u8 = 0x01;
    pub const STATISTICS: u8 = 0x07;
    pub const VALUES: u8 = 0x0d;
    pub const GOODBYE: u8 = 0x13;
    pub const HAPTICS: u8 = 0x16;
}

pub mod video {
    pub const DESCRIPTOR: u8 = 0x02;
    pub const PACKET: u8 = 0x03;
    /// From the headset: packets it wants again, and how far it has got with the frames.
    pub const RETRANSMIT_REQUEST: u8 = 0x05;
    pub const FRAME_REPORT: u8 = 0x0f;
}

/// The link probe, outside the framing: a mark, 2, then 1 to ask for it back or 2 for the answer.
const PROBE_MARK: u32 = 0x459b_4c33;

#[derive(Clone, Debug, PartialEq)]
pub struct Element {
    pub class: u8,
    pub kind: u8,
    pub id: u16,
    pub index: u16,
    pub value: Vec<u8>,
}

impl Element {
    pub fn new(class: u8, kind: u8, value: Vec<u8>) -> Self {
        Self { class, kind, id: 0, index: 0, value }
    }

    pub fn size(&self) -> usize {
        ELEMENT_HEADER + self.value.len()
    }
}

/// The session id a `MID0` token's key makes: its first three bytes, tagged 0x01.
pub fn session_id(token: &[u8; 32]) -> u32 {
    u32::from(token[0]) | u32::from(token[1]) << 8 | u32::from(token[2]) << 16 | 0x0100_0000
}

fn fold(datagram: &[u8], seed: u32) -> u32 {
    let mut accumulator = seed;
    let words = datagram.chunks_exact(4);
    let rest = words.remainder();
    for word in words {
        accumulator ^= u32::from_le_bytes(word.try_into().unwrap());
    }
    for (i, &byte) in rest.iter().enumerate() {
        accumulator ^= u32::from(byte) << (i * 8);
    }
    accumulator
}

pub fn pack(session_id: u32, sequence: u16, elements: &[Element]) -> Vec<u8> {
    let mut datagram = Vec::with_capacity(HEADER + elements.iter().map(Element::size).sum::<usize>());
    datagram.extend_from_slice(&sequence.to_le_bytes());
    datagram.extend_from_slice(&[0, 0, 0, 0, 0, 0]);
    for element in elements {
        datagram.push(element.class);
        datagram.push(element.kind);
        datagram.extend_from_slice(&element.id.to_le_bytes());
        datagram.extend_from_slice(&(element.value.len() as u16).to_le_bytes());
        datagram.extend_from_slice(&element.index.to_le_bytes());
        datagram.extend_from_slice(&element.value);
    }
    let check = fold(&datagram, session_id);
    datagram[4..8].copy_from_slice(&check.to_le_bytes());
    datagram
}

/// The elements of one of this session's datagrams; None for anything else.
pub fn unpack(session_id: u32, datagram: &[u8]) -> Option<Vec<Element>> {
    if datagram.len() < HEADER || fold(datagram, session_id) != 0 {
        return None;
    }
    let mut elements = Vec::new();
    let mut offset = HEADER;
    while datagram.len() - offset >= ELEMENT_HEADER {
        let length = u16::from_le_bytes([datagram[offset + 4], datagram[offset + 5]]) as usize;
        let end = offset + ELEMENT_HEADER + length;
        if length > MAX_VALUE || end > datagram.len() {
            break;
        }
        if datagram[offset] != 0 {
            elements.push(Element {
                class: datagram[offset],
                kind: datagram[offset + 1],
                id: u16::from_le_bytes([datagram[offset + 2], datagram[offset + 3]]),
                index: u16::from_le_bytes([datagram[offset + 6], datagram[offset + 7]]),
                value: datagram[offset + ELEMENT_HEADER..end].to_vec(),
            });
        }
        offset = end;
    }
    Some(elements)
}

/// The answer to a link probe asking for one; None for anything else.
pub fn probe_answer(datagram: &[u8]) -> Option<Vec<u8>> {
    let word = |i: usize| datagram.get(i * 4..i * 4 + 4).map(|w| u32::from_le_bytes(w.try_into().unwrap()));
    if word(0)? != PROBE_MARK || word(1)? != 2 || word(2)? != 1 {
        return None;
    }
    let mut answer = datagram.to_vec();
    answer[8] = 2;
    Some(answer)
}

pub fn is_probe(datagram: &[u8]) -> bool {
    datagram.len() >= 12 && u32::from_le_bytes(datagram[..4].try_into().unwrap()) == PROBE_MARK
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn packs_and_unpacks_with_the_check() {
        let id = session_id(&[0x12, 0x34, 0x56, 0x78].iter().chain([0u8; 28].iter()).copied().collect::<Vec<_>>().try_into().unwrap());
        assert_eq!(id, 0x0156_3412);
        let elements = vec![
            Element { class: 5, kind: 3, id: 7, index: 0x801, value: vec![1, 2, 3] },
            Element::new(3, 0x0d, vec![9; 10]),
        ];
        let datagram = pack(id, 42, &elements);
        assert_eq!(&datagram[..4], &[42, 0, 0, 0]);
        assert_eq!(unpack(id, &datagram).unwrap(), elements);
        // Another session's datagram, or a damaged one, isn't ours.
        assert!(unpack(id ^ 1, &datagram).is_none());
        let mut damaged = datagram.clone();
        damaged[10] ^= 0x40;
        assert!(unpack(id, &damaged).is_none());
    }

    #[test]
    fn answers_probes() {
        let mut probe = Vec::new();
        for word in [PROBE_MARK, 2, 1, 0] {
            probe.extend_from_slice(&word.to_le_bytes());
        }
        let answer = probe_answer(&probe).unwrap();
        assert_eq!(answer[8], 2);
        assert!(is_probe(&answer) && probe_answer(&answer).is_none());
    }
}
