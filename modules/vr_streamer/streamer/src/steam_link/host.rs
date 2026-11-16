//! The host's side of discovery, pairing and starting a session (OpenVRLink's
//! docs/remoteplay/): announces this Mac every second as "<computer> (VR)", a VR-capable host of
//! its own beside Steam's (which holds Steam's port and says it can't do VR), and answers the
//! headset's requests on a port of its own, which the announcement names.
//!
//! A headset pairs once: it shows a PIN, the game's runtime asks the player for it (the
//! `pin_needed` status line; the PIN comes back on stdin), and both sides make a shared secret
//! from it. After that each session request is checked against that secret, and answered with a
//! session key and the session string the headset's VR client starts from.

use super::{
    broadcast::{self, authorization, kind, streaming},
    crypto::{base64url, hex, pairing_exchange, random_bytes, symmetric_decrypt, symmetric_encrypt},
    devices::{Device, Devices},
    protobuf::{Message, Writer},
};
use crate::status::status;
use log::{info, warn};
use std::{
    net::{IpAddr, Ipv4Addr, SocketAddr, SocketAddrV4, UdpSocket},
    sync::mpsc::{self, Receiver, Sender, TryRecvError},
    thread,
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};

/// The ports tried for the host's requests and for the stream, first free one taken.
const REQUEST_PORTS: std::ops::Range<u16> = 27037..27047;
const STREAM_PORTS: std::ops::Range<u16> = 10400..10410;
const ANNOUNCE_EVERY: Duration = Duration::from_secs(1);

/// A session the headset was told about, ready for the stream.
pub struct SessionStart {
    pub socket: UdpSocket,
    pub headset: IpAddr,
    pub device_name: String,
    /// The 32 bytes after `MID0` in the session token.
    pub token: [u8; 32],
}

/// What the player answered to the PIN prompt.
pub enum PinAnswer {
    Pin(String),
    Cancel,
}

pub struct Host {
    pub sessions: Receiver<SessionStart>,
    pins: Sender<PinAnswer>,
}

impl Host {
    pub fn answer_pin(&self, answer: PinAnswer) {
        self.pins.send(answer).ok();
    }
}

/// Starts announcing this Mac as the host kept in `store` (devices::shared_file); None if no port is free.
pub fn start(store: std::path::PathBuf) -> Option<Host> {
    let requests = REQUEST_PORTS.clone().find_map(|port| UdpSocket::bind(("0.0.0.0", port)).ok())?;
    let stream = STREAM_PORTS.clone().find_map(|port| UdpSocket::bind(("0.0.0.0", port)).ok())?;
    requests.set_broadcast(true).ok()?;
    requests.set_read_timeout(Some(Duration::from_millis(50))).ok()?;
    let (sessions_tx, sessions) = mpsc::channel();
    let (pins, pins_rx) = mpsc::channel();
    let devices = Devices::load(store);
    let mut state = State {
        host_id: devices.host_id,
        devices,
        instance_id: u64::from_le_bytes(random_bytes()),
        requests,
        stream: Some(stream),
        template: None,
        pairing: None,
        proof: None,
        sessions: sessions_tx,
        busy: false,
    };
    thread::Builder::new()
        .name("steam-link-host".into())
        .spawn(move || state.run(pins_rx))
        .ok()?;
    Some(Host { sessions, pins })
}

struct Pairing {
    device_id: u64,
    device_name: String,
    headset: SocketAddr,
    masked_key: Vec<u8>,
    /// The response once the player has typed the PIN, and the secret it made.
    answered: Option<(Vec<u8>, [u8; 32])>,
}

struct Proof {
    device_id: u64,
    request_id: u64,
    challenge: [u8; 16],
    /// The success response, once the proof checked out: repeated to repeated requests.
    success: Option<Vec<u8>>,
}

struct State {
    devices: Devices,
    host_id: u64,
    instance_id: u64,
    requests: UdpSocket,
    /// The stream's socket, until a session takes it.
    stream: Option<UdpSocket>,
    /// Steam's own status for this Mac, which ours copies.
    template: Option<Vec<u8>>,
    pairing: Option<Pairing>,
    proof: Option<Proof>,
    sessions: Sender<SessionStart>,
    /// Set once a headset streams: other requests are told the host is busy.
    busy: bool,
}

impl State {
    fn run(&mut self, pins: Receiver<PinAnswer>) {
        // Steam's status, or as it was last seen when Steam isn't running.
        let live = steam_status(&self.requests);
        if let Some(status) = &live {
            self.devices.remember_steam_status(status);
        }
        self.template = live.clone().or_else(|| self.devices.steam_status().map(<[u8]>::to_vec));
        let port = self.requests.local_addr().map(|a| a.port()).unwrap_or(0);
        let source = match (&live, &self.template) {
            (Some(_), _) => "",
            (None, Some(_)) => " (Steam isn't running: as it last announced this Mac)",
            (None, None) => " (Steam hasn't been seen on this Mac)",
        };
        info!("Steam Link: announcing \"{}\" on port {port}{source}", self.host_name());
        let mut next_announcement = Instant::now();
        let mut buffer = [0u8; 2048];
        loop {
            if Instant::now() >= next_announcement {
                self.announce();
                next_announcement = Instant::now() + ANNOUNCE_EVERY;
            }
            match pins.try_recv() {
                Ok(answer) => self.pin_answered(answer),
                Err(TryRecvError::Disconnected) => return,
                Err(TryRecvError::Empty) => {}
            }
            let Ok((length, from)) = self.requests.recv_from(&mut buffer) else { continue };
            if let Some(datagram) = broadcast::decode(&buffer[..length]) {
                self.handle(datagram, from);
            }
        }
    }

    fn host_name(&self) -> String {
        let steam_name = self.template.as_deref().and_then(Message::parse).and_then(|m| m.string(4));
        format!("{} (VR)", steam_name.unwrap_or_else(computer_name))
    }

    fn steam_id(&self) -> Option<u64> {
        let status = Message::parse(self.template.as_deref()?)?;
        let user = status.bytes(9)?;
        Message::parse(user)?.fixed64(1)
    }

    /// Steam's status with this host's name, port and abilities; or one of its own without Steam.
    fn status(&self) -> Vec<u8> {
        let port = self.requests.local_addr().map(|a| a.port()).unwrap_or(0);
        let now = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
        let busy = self.busy;
        let mut out = Writer::new();
        let template = self.template.as_deref().and_then(Message::parse);
        // Fields this host sets; Steam's others (the OS, Steam's version, the account) carry over.
        const OURS: [u32; 7] = [3, 4, 12, 14, 18, 20, 26];
        if let Some(template) = &template {
            for (number, value) in &template.fields {
                if !OURS.contains(number) {
                    out.value(*number, *value);
                }
            }
        } else {
            // As Steam on a Mac announces itself: protocol 8 (from 6), its services, macOS
            // (-67), 64-bit, the public universe.
            out.varint(1, 8).varint(2, 6).varint(6, 98).varint(7, (-67i64) as u64).varint(8, 1).varint(11, 1);
        }
        for address in lan_addresses() {
            out.string(20, &address.0.to_string());
        }
        out.varint(3, u64::from(port))
            .string(4, &self.host_name())
            .varint(12, now)
            .varint(14, 0)
            .varint(18, busy as u64)
            .varint(26, broadcast::VR_LINK_AVAILABLE);
        out.finish()
    }

    fn announce(&self) {
        let packet = broadcast::encode(self.host_id, kind::STATUS, Some(self.instance_id), &self.status());
        let mut destinations: Vec<Ipv4Addr> = lan_addresses().into_iter().map(|(_, broadcast)| broadcast).collect();
        destinations.push(Ipv4Addr::BROADCAST);
        for destination in destinations {
            // macOS sometimes refuses the limited broadcast; each network's own is enough.
            self.requests.send_to(&packet, (destination, broadcast::STEAM_PORT)).ok();
        }
    }

    fn reply(&self, to: SocketAddr, message_kind: u64, body: &[u8]) {
        self.requests.send_to(&broadcast::encode(self.host_id, message_kind, None, body), to).ok();
    }

    fn handle(&mut self, datagram: broadcast::Datagram, from: SocketAddr) {
        let Some(body) = Message::parse(&datagram.body) else { return };
        match datagram.kind {
            kind::STREAMING_REQUEST => self.streaming_request(datagram.client_id, &body, from),
            kind::PROOF_RESPONSE => self.proof_response(datagram.client_id, &body, from),
            kind::AUTHORIZATION_REQUEST => self.authorization_request(datagram.client_id, &body, from),
            kind::AUTHORIZATION_CONFIRMED => self.authorization_confirmed(datagram.client_id, &body),
            kind::AUTHORIZATION_CANCEL => {
                if self.pairing.as_ref().is_some_and(|p| p.device_id == datagram.client_id) {
                    self.pairing = None;
                    status("pin_cancelled", serde_json::json!({}));
                }
            }
            kind::STREAMING_CANCEL => {
                if self.proof.as_ref().is_some_and(|p| p.device_id == datagram.client_id && p.success.is_none()) {
                    self.proof = None;
                }
            }
            _ => {}
        }
    }

    fn streaming_response(&self, to: SocketAddr, request_id: u64, result: u64) {
        self.reply(to, kind::STREAMING_RESPONSE, &Writer::new().varint(1, request_id).varint(2, result).finish());
    }

    fn streaming_request(&mut self, device_id: u64, body: &Message, from: SocketAddr) {
        // Requests for another host (Steam's, say) and flat streaming aren't ours.
        if body.varint(13) != Some(self.host_id) || body.varint(20) != Some(broadcast::STREAM_INTERFACE_STEAMVR) {
            return;
        }
        let request_id = body.varint(1).unwrap_or(0);
        let token = body.bytes(7).unwrap_or_default();
        let paired = self.devices.get(device_id).is_some_and(|device| {
            symmetric_decrypt(token, &device.secret).as_deref() == Some(&device_id.to_le_bytes()[..])
        });
        if !paired {
            self.streaming_response(from, request_id, streaming::UNAUTHORIZED);
            return;
        }
        if let Some(proof) = &self.proof {
            if proof.device_id == device_id && proof.request_id == request_id {
                match &proof.success {
                    Some(response) => self.reply(from, kind::STREAMING_RESPONSE, response),
                    None => self.send_proof_request(from),
                }
                return;
            }
        }
        if self.busy || self.stream.is_none() {
            self.streaming_response(from, request_id, streaming::BUSY);
            return;
        }
        self.proof = Some(Proof { device_id, request_id, challenge: random_bytes(), success: None });
        self.send_proof_request(from);
    }

    fn send_proof_request(&self, to: SocketAddr) {
        let Some(proof) = &self.proof else { return };
        let body = Writer::new().bytes(1, &proof.challenge).varint(2, proof.request_id).varint(3, 0).finish();
        self.reply(to, kind::PROOF_REQUEST, &body);
    }

    fn proof_response(&mut self, device_id: u64, body: &Message, from: SocketAddr) {
        let Some(device) = self.devices.get(device_id).cloned() else { return };
        let Some((request_id, challenge)) = self
            .proof
            .as_ref()
            .filter(|p| p.device_id == device_id && p.success.is_none())
            .map(|p| (p.request_id, p.challenge))
        else {
            return;
        };
        if body.varint(2) != Some(request_id) {
            return;
        }
        if symmetric_decrypt(body.bytes(1).unwrap_or_default(), &device.secret).as_deref() != Some(&challenge[..]) {
            warn!("Steam Link: {} didn't prove it's the headset paired as that", device.name);
            self.streaming_response(from, request_id, streaming::UNAUTHORIZED);
            self.proof = None;
            return;
        }
        let Some(socket) = self.stream.take() else { return };
        let port = socket.local_addr().map(|a| a.port()).unwrap_or(0);
        // The headset puts its view of this host's address in front of a string starting with '~'.
        let token: [u8; 32] = random_bytes();
        let session_string = format!("~{port}~{port}~0,0,0~~~~MID0{}", base64url(&token));
        let session_key: [u8; 16] = random_bytes();
        let response = Writer::new()
            .varint(1, request_id)
            .varint(2, streaming::SUCCESS)
            .varint(3, 0)
            .bytes(4, &symmetric_encrypt(&session_key, &device.secret))
            .varint(6, 0)
            .string(7, "")
            .string(8, &hex(&symmetric_encrypt(session_string.as_bytes(), &session_key)))
            .finish();
        if let Some(proof) = &mut self.proof {
            proof.success = Some(response.clone());
        }
        self.streaming_response(from, request_id, streaming::IN_PROGRESS);
        self.reply(from, kind::STREAMING_RESPONSE, &response);
        self.busy = true;
        info!("Steam Link: starting a session with {} on port {port}", device.name);
        self.sessions.send(SessionStart { socket, headset: from.ip(), device_name: device.name, token }).ok();
    }

    fn authorization_request(&mut self, device_id: u64, body: &Message, from: SocketAddr) {
        let masked_key = body.bytes(4).unwrap_or_default().to_vec();
        if masked_key.len() != 32 {
            // The older route through Steam's servers: not one a host of its own can take.
            self.reply(from, kind::AUTHORIZATION_RESPONSE, &Writer::new().varint(1, authorization::DENIED).finish());
            return;
        }
        let same = self.pairing.as_ref().is_some_and(|p| p.device_id == device_id && p.masked_key == masked_key);
        if !same {
            let device_name = body.string(2).filter(|n| !n.is_empty()).unwrap_or_else(|| "A headset".to_owned());
            info!("Steam Link: {device_name} asks to pair");
            status("pin_needed", serde_json::json!({ "device": device_name }));
            self.pairing = Some(Pairing { device_id, device_name, headset: from, masked_key, answered: None });
        }
        let pairing = self.pairing.as_mut().unwrap();
        pairing.headset = from;
        let response = match &pairing.answered {
            Some((response, _)) => response.clone(),
            None => Writer::new().varint(1, authorization::IN_PROGRESS).finish(),
        };
        self.reply(from, kind::AUTHORIZATION_RESPONSE, &response);
    }

    fn pin_answered(&mut self, answer: PinAnswer) {
        let steam_id = self.steam_id();
        let Some(pairing) = self.pairing.as_mut().filter(|p| p.answered.is_none()) else { return };
        let headset = pairing.headset;
        let pin = match answer {
            PinAnswer::Pin(pin) => pin,
            PinAnswer::Cancel => {
                self.reply(headset, kind::AUTHORIZATION_RESPONSE, &Writer::new().varint(1, authorization::CANCELED).finish());
                self.pairing = None;
                return;
            }
        };
        let Some(pairing) = self.pairing.as_mut() else { return };
        let Some((masked_public, shared)) = pairing_exchange(&pairing.masked_key, &pin) else { return };
        let mut response = Writer::new();
        response.varint(1, authorization::SUCCESS);
        if let Some(steam_id) = steam_id {
            response.fixed64(2, steam_id);
        }
        let response = response
            .bytes(3, &masked_public)
            .bytes(4, &symmetric_encrypt(&pairing.device_id.to_le_bytes(), &shared))
            .finish();
        pairing.answered = Some((response.clone(), shared));
        let headset = pairing.headset;
        self.reply(headset, kind::AUTHORIZATION_RESPONSE, &response);
    }

    fn authorization_confirmed(&mut self, device_id: u64, body: &Message) {
        let Some(pairing) = self.pairing.take().filter(|p| p.device_id == device_id) else { return };
        let Some((_, secret)) = pairing.answered else {
            self.pairing = Some(pairing);
            return;
        };
        if body.varint(1) == Some(authorization::SUCCESS) {
            info!("Steam Link: paired with {}", pairing.device_name);
            status("paired", serde_json::json!({ "device": pairing.device_name }));
            self.devices.add(device_id, Device { name: pairing.device_name, secret });
        } else {
            // A wrong PIN: the headset asks again, and so does the game.
            warn!("Steam Link: {} refused the pairing (a wrong PIN?)", pairing.device_name);
            status("pin_wrong", serde_json::json!({ "device": pairing.device_name }));
        }
    }
}

/// Steam's own status for this Mac, asked for directly; None when Steam isn't running.
fn steam_status(socket: &UdpSocket) -> Option<Vec<u8>> {
    let probe = broadcast::encode(u64::from_le_bytes(random_bytes()), kind::DISCOVERY, None, &Writer::new().varint(1, 1).finish());
    let until = Instant::now() + Duration::from_secs(1);
    for (address, _) in lan_addresses() {
        socket.send_to(&probe, SocketAddrV4::new(address, broadcast::STEAM_PORT)).ok();
    }
    let mut buffer = [0u8; 2048];
    while Instant::now() < until {
        let Ok((length, _)) = socket.recv_from(&mut buffer) else { continue };
        if let Some(datagram) = broadcast::decode(&buffer[..length]).filter(|d| d.kind == kind::STATUS) {
            if Message::parse(&datagram.body).is_some() {
                return Some(datagram.body);
            }
        }
    }
    None
}

/// The computer's network name, as Steam names it ("Ivos-Mac-Studio").
fn computer_name() -> String {
    let mut buffer = [0u8; 256];
    let length = unsafe {
        if libc::gethostname(buffer.as_mut_ptr().cast(), buffer.len()) != 0 {
            return "Mac".to_owned();
        }
        buffer.iter().position(|&b| b == 0).unwrap_or(buffer.len())
    };
    let name = String::from_utf8_lossy(&buffer[..length]).into_owned();
    name.strip_suffix(".local").map(str::to_owned).unwrap_or(name)
}

/// This Mac's IPv4 addresses on its networks, each with that network's broadcast address.
fn lan_addresses() -> Vec<(Ipv4Addr, Ipv4Addr)> {
    let mut out = Vec::new();
    unsafe {
        let mut list: *mut libc::ifaddrs = std::ptr::null_mut();
        if libc::getifaddrs(&mut list) != 0 {
            return out;
        }
        let mut entry = list;
        while !entry.is_null() {
            let interface = &*entry;
            let flags = interface.ifa_flags as libc::c_int;
            let usable = flags & libc::IFF_UP != 0 && flags & libc::IFF_LOOPBACK == 0 && flags & libc::IFF_BROADCAST != 0;
            if usable && !interface.ifa_addr.is_null() && i32::from((*interface.ifa_addr).sa_family) == libc::AF_INET && !interface.ifa_dstaddr.is_null() {
                let address = &*(interface.ifa_addr as *const libc::sockaddr_in);
                let broadcast = &*(interface.ifa_dstaddr as *const libc::sockaddr_in);
                out.push((
                    Ipv4Addr::from(u32::from_be(address.sin_addr.s_addr)),
                    Ipv4Addr::from(u32::from_be(broadcast.sin_addr.s_addr)),
                ));
            }
            entry = interface.ifa_next;
        }
        libc::freeifaddrs(list);
    }
    out.sort();
    out.dedup();
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn finds_this_macs_networks() {
        // Whatever machine runs the tests has at least one, with a broadcast address in it.
        for (address, broadcast) in lan_addresses() {
            assert!(!address.is_loopback());
            assert!(!broadcast.is_unspecified());
        }
        assert!(!computer_name().is_empty());
    }

    #[test]
    fn copies_steams_status_with_ours_on_top() {
        let template = Writer::new()
            .varint(1, 8)
            .varint(3, 27036)
            .string(4, "Ivos-Mac-Studio")
            .bytes(9, &Writer::new().fixed64(1, 0x0110_0001_0000_0001).varint(2, 5).finish())
            .varint(26, 2)
            .finish();
        let (sessions, _) = mpsc::channel();
        let store = std::env::temp_dir().join(format!("steam_link_host_test_{}.json", std::process::id()));
        let state = State {
            devices: Devices::load(store.clone()),
            host_id: 1,
            instance_id: 2,
            requests: UdpSocket::bind("127.0.0.1:0").unwrap(),
            stream: None,
            template: Some(template),
            pairing: None,
            proof: None,
            sessions,
            busy: false,
        };
        let status = state.status();
        let message = Message::parse(&status).unwrap();
        assert_eq!(message.string(4).as_deref(), Some("Ivos-Mac-Studio (VR)"));
        assert_eq!(message.varint(26), Some(broadcast::VR_LINK_AVAILABLE));
        assert_eq!(message.varint(1), Some(8));
        assert_ne!(message.varint(3), Some(27036));
        assert_eq!(state.steam_id(), Some(0x0110_0001_0000_0001));
        // Only one of each field we set.
        assert_eq!(message.fields.iter().filter(|(n, _)| *n == 26).count(), 1);
        std::fs::remove_file(store).ok();
    }
}
