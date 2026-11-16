//! This Mac as a Steam Link host, kept for every game: its host id (the headset keeps its pairing
//! under it) and the headsets paired with it, each with the secret pairing made. A headset pairs
//! once, for every game that streams with Purr's streamer.

use std::{collections::BTreeMap, path::PathBuf};

/// Where it's kept: the user's, for every game (and anything else running the streamer).
pub fn shared_file() -> PathBuf {
    let home = std::env::var_os("HOME").map(PathBuf::from).unwrap_or_else(std::env::temp_dir);
    home.join("Library/Application Support/Purr/VR Streaming/steam_link.json")
}

#[derive(Clone)]
pub struct Device {
    pub name: String,
    pub secret: [u8; 32],
}

pub struct Devices {
    path: PathBuf,
    pub host_id: u64,
    devices: BTreeMap<u64, Device>,
    /// Steam's own status for this Mac, as last seen: announced in its place when Steam isn't
    /// running, since the headset turns down a host that doesn't look like Steam.
    steam_status: Option<Vec<u8>>,
}

impl Devices {
    /// The host and its headsets from `path`; a new host id the first time.
    pub fn load(path: PathBuf) -> Self {
        let saved = std::fs::read_to_string(&path).ok().and_then(|text| serde_json::from_str::<serde_json::Value>(&text).ok());
        let host_id = saved.as_ref().and_then(|s| s.get("host_id")?.as_str()?.parse::<u64>().ok());
        let mut devices = BTreeMap::new();
        if let Some(serde_json::Value::Object(entries)) = saved.as_ref().and_then(|s| s.get("devices")) {
            for (id, entry) in entries {
                let secret = entry.get("secret").and_then(|s| s.as_str()).and_then(super::crypto::unhex);
                if let (Ok(id), Some(Ok(secret))) = (id.parse::<u64>(), secret.map(<[u8; 32]>::try_from)) {
                    let name = entry.get("name").and_then(|n| n.as_str()).unwrap_or("").to_owned();
                    devices.insert(id, Device { name, secret });
                }
            }
        }
        let steam_status = saved.as_ref().and_then(|s| s.get("steam_status")?.as_str()).and_then(super::crypto::unhex);
        let loaded = Self { path, host_id: host_id.unwrap_or_else(|| u64::from_le_bytes(super::crypto::random_bytes())), devices, steam_status };
        if host_id.is_none() {
            loaded.save();
        }
        loaded
    }

    pub fn get(&self, id: u64) -> Option<&Device> {
        self.devices.get(&id)
    }

    pub fn steam_status(&self) -> Option<&[u8]> {
        self.steam_status.as_deref()
    }

    pub fn remember_steam_status(&mut self, status: &[u8]) {
        if self.steam_status.as_deref() != Some(status) {
            self.steam_status = Some(status.to_vec());
            self.save();
        }
    }

    pub fn add(&mut self, id: u64, device: Device) {
        self.devices.insert(id, device);
        self.save();
    }

    fn save(&self) {
        let devices: serde_json::Map<_, _> = self
            .devices
            .iter()
            .map(|(id, d)| (id.to_string(), serde_json::json!({ "name": d.name, "secret": super::crypto::hex(&d.secret) })))
            .collect();
        // The host id as text: JSON numbers lose 64-bit precision in other readers.
        let mut saved = serde_json::json!({ "host_id": self.host_id.to_string(), "devices": devices });
        if let Some(status) = &self.steam_status {
            saved["steam_status"] = super::crypto::hex(status).into();
        }
        let text = serde_json::to_string_pretty(&saved).unwrap_or_default();
        let written = self.path.parent().map_or(Ok(()), std::fs::create_dir_all).and_then(|()| std::fs::write(&self.path, text));
        if let Err(e) = written {
            log::error!("cannot save the paired headsets in {}: {e}", self.path.display());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_host_and_its_headsets_last() {
        let folder = std::env::temp_dir().join(format!("steam_link_devices_{}", std::process::id()));
        let path = folder.join("nested/steam_link.json");
        let mut devices = Devices::load(path.clone());
        let host_id = devices.host_id;
        assert!(devices.get(7).is_none());
        // The same host every time, even before a headset pairs.
        assert_eq!(Devices::load(path.clone()).host_id, host_id);
        devices.add(7, Device { name: "Quest 3".into(), secret: [9; 32] });
        devices.remember_steam_status(&[8, 8]);
        let again = Devices::load(path);
        assert_eq!(again.steam_status(), Some(&[8u8, 8][..]));
        assert_eq!(again.host_id, host_id);
        assert_eq!(again.get(7).unwrap().secret, [9; 32]);
        assert_eq!(again.get(7).unwrap().name, "Quest 3");
        std::fs::remove_dir_all(&folder).unwrap();
    }
}
