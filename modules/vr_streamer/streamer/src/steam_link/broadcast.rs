//! Steam's Remote Play broadcast datagrams (OpenVRLink's docs/remoteplay/broadcast-packet.md):
//! discovery, pairing and session requests all travel as one framing on UDP, a header message
//! naming the kind of body that follows.
//!
//! ```text
//! ff ff ff ff 21 4c 5f a0   magic
//! u32le header length, CMsgRemoteClientBroadcastHeader
//! u32le body length, the body
//! ```

use super::protobuf::{Message, Writer};

pub const STEAM_PORT: u16 = 27036;
const MAGIC: [u8; 8] = [0xff, 0xff, 0xff, 0xff, 0x21, 0x4c, 0x5f, 0xa0];

/// `ERemoteClientBroadcastMsg`: what a datagram's body is.
pub mod kind {
    pub const DISCOVERY: u64 = 0;
    pub const STATUS: u64 = 1;
    pub const AUTHORIZATION_REQUEST: u64 = 3;
    pub const AUTHORIZATION_RESPONSE: u64 = 4;
    pub const STREAMING_REQUEST: u64 = 5;
    pub const STREAMING_RESPONSE: u64 = 6;
    pub const PROOF_REQUEST: u64 = 7;
    pub const PROOF_RESPONSE: u64 = 8;
    pub const AUTHORIZATION_CANCEL: u64 = 9;
    pub const STREAMING_CANCEL: u64 = 10;
    pub const AUTHORIZATION_CONFIRMED: u64 = 14;
}

/// `ERemoteDeviceAuthorizationResult`.
pub mod authorization {
    pub const SUCCESS: u64 = 0;
    pub const DENIED: u64 = 1;
    pub const IN_PROGRESS: u64 = 5;
    pub const CANCELED: u64 = 8;
}

/// `ERemoteDeviceStreamingResult`.
pub mod streaming {
    pub const SUCCESS: u64 = 0;
    pub const UNAUTHORIZED: u64 = 1;
    pub const BUSY: u64 = 4;
    pub const IN_PROGRESS: u64 = 5;
}

/// `EStreamInterface::SteamVR`: what a VR session request asks for.
pub const STREAM_INTERFACE_STEAMVR: u64 = 4;
/// `EVRLinkCaps::Available`.
pub const VR_LINK_AVAILABLE: u64 = 1;

/// One datagram: who sent it (the header's client id: a headset's device id, or a host's id),
/// what it is, and the body's bytes.
pub struct Datagram {
    pub client_id: u64,
    pub kind: u64,
    pub body: Vec<u8>,
}

pub fn encode(client_id: u64, kind: u64, instance_id: Option<u64>, body: &[u8]) -> Vec<u8> {
    let mut header = Writer::new();
    header.varint(1, client_id).varint(2, kind);
    if let Some(instance) = instance_id {
        header.varint(3, instance);
    }
    let header = header.finish();
    let mut out = MAGIC.to_vec();
    out.extend_from_slice(&(header.len() as u32).to_le_bytes());
    out.extend_from_slice(&header);
    out.extend_from_slice(&(body.len() as u32).to_le_bytes());
    out.extend_from_slice(body);
    out
}

pub fn decode(data: &[u8]) -> Option<Datagram> {
    if data.len() < 16 || data[..8] != MAGIC {
        return None;
    }
    let header_length = u32::from_le_bytes(data[8..12].try_into().ok()?) as usize;
    let header_end = 12usize.checked_add(header_length)?;
    let header = Message::parse(data.get(12..header_end)?)?;
    let body_length = u32::from_le_bytes(data.get(header_end..header_end + 4)?.try_into().ok()?) as usize;
    let body = data.get(header_end + 4..header_end + 4 + body_length)?;
    Some(Datagram { client_id: header.varint(1).unwrap_or(0), kind: header.varint(2)?, body: body.to_vec() })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_captured_discovery_probe() {
        // docs/remoteplay/discovery.md: header {client_id, msg_type 0}, body {seq_num 1}.
        let probe = encode(0x0102, kind::DISCOVERY, None, &[0x08, 0x01]);
        assert_eq!(&probe[..8], &MAGIC);
        let decoded = decode(&probe).unwrap();
        assert_eq!((decoded.client_id, decoded.kind, decoded.body.as_slice()), (0x0102, kind::DISCOVERY, &[0x08u8, 0x01][..]));
        assert!(decode(&probe[..probe.len() - 1]).is_none());
        assert!(decode(b"not this protocol at all").is_none());
    }
}
