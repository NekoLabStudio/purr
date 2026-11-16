//! Steam's cryptography for pairing and starting a session (OpenVRLink's
//! docs/remoteplay/crypto.md): AES-CBC with the IV sent encrypted in front, and the PIN-masked
//! X25519 exchange that makes a paired headset's secret.

use aes::{
    cipher::{Array, BlockCipherDecrypt, BlockCipherEncrypt, KeyInit},
    Aes128, Aes256,
};
use sha2::{Digest, Sha256};

pub fn sha256(data: &[u8]) -> [u8; 32] {
    Sha256::digest(data).into()
}

pub fn random_bytes<const N: usize>() -> [u8; N] {
    let mut bytes = [0u8; N];
    getrandom::fill(&mut bytes).expect("the system's random source");
    bytes
}

enum Cipher {
    Aes128(Aes128),
    Aes256(Aes256),
}

impl Cipher {
    fn new(key: &[u8]) -> Option<Self> {
        match key.len() {
            16 => Some(Self::Aes128(Aes128::new(&Array::try_from(key).ok()?))),
            32 => Some(Self::Aes256(Aes256::new(&Array::try_from(key).ok()?))),
            _ => None,
        }
    }

    fn encrypt(&self, block: &mut [u8; 16]) {
        let mut array = Array::from(*block);
        match self {
            Self::Aes128(c) => c.encrypt_block(&mut array),
            Self::Aes256(c) => c.encrypt_block(&mut array),
        }
        *block = array.into();
    }

    fn decrypt(&self, block: &mut [u8; 16]) {
        let mut array = Array::from(*block);
        match self {
            Self::Aes128(c) => c.decrypt_block(&mut array),
            Self::Aes256(c) => c.decrypt_block(&mut array),
        }
        *block = array.into();
    }
}

/// `CCrypto::SymmetricEncrypt`: a fresh IV, AES-ECB-encrypted and put in front, then the
/// plaintext AES-CBC-encrypted with PKCS#7 padding. The key is 16 or 32 bytes.
pub fn symmetric_encrypt(plaintext: &[u8], key: &[u8]) -> Vec<u8> {
    symmetric_encrypt_with_iv(plaintext, key, random_bytes())
}

fn symmetric_encrypt_with_iv(plaintext: &[u8], key: &[u8], iv: [u8; 16]) -> Vec<u8> {
    let cipher = Cipher::new(key).expect("a 16- or 32-byte key");
    let mut wrapped_iv = iv;
    cipher.encrypt(&mut wrapped_iv);
    let mut out = wrapped_iv.to_vec();
    let padding = 16 - plaintext.len() % 16;
    let mut padded = plaintext.to_vec();
    padded.extend(std::iter::repeat_n(padding as u8, padding));
    let mut previous = iv;
    for chunk in padded.chunks_exact(16) {
        let mut block: [u8; 16] = chunk.try_into().unwrap();
        for (b, p) in block.iter_mut().zip(previous) {
            *b ^= p;
        }
        cipher.encrypt(&mut block);
        out.extend_from_slice(&block);
        previous = block;
    }
    out
}

/// The other way; None for a wrong key (bad padding: there is no MAC) or a short input.
pub fn symmetric_decrypt(ciphertext: &[u8], key: &[u8]) -> Option<Vec<u8>> {
    let cipher = Cipher::new(key)?;
    if ciphertext.len() < 32 || ciphertext.len() % 16 != 0 {
        return None;
    }
    let mut iv: [u8; 16] = ciphertext[..16].try_into().unwrap();
    cipher.decrypt(&mut iv);
    let mut out = Vec::with_capacity(ciphertext.len() - 16);
    let mut previous = iv;
    for chunk in ciphertext[16..].chunks_exact(16) {
        let encrypted: [u8; 16] = chunk.try_into().unwrap();
        let mut block = encrypted;
        cipher.decrypt(&mut block);
        for (b, p) in block.iter_mut().zip(previous) {
            *b ^= p;
        }
        out.extend_from_slice(&block);
        previous = encrypted;
    }
    let padding = *out.last()? as usize;
    if padding == 0 || padding > 16 || out[out.len() - padding..].iter().any(|&b| b as usize != padding) {
        return None;
    }
    out.truncate(out.len() - padding);
    Some(out)
}

/// The PIN's mask over each side's public key: SHA-256 of its four digits.
fn pin_mask(pin: &str) -> [u8; 32] {
    sha256(pin.as_bytes())
}

fn xor32(a: &[u8; 32], b: &[u8; 32]) -> [u8; 32] {
    std::array::from_fn(|i| a[i] ^ b[i])
}

/// The host's half of pairing, given the headset's masked public key and the PIN the player
/// typed: the host's masked public key to send back, and the shared secret
/// (`SHA-256(X25519(...))`). A wrong PIN makes a different secret, which the headset finds out.
pub fn pairing_exchange(peer_masked_key: &[u8], pin: &str) -> Option<([u8; 32], [u8; 32])> {
    let masked: [u8; 32] = peer_masked_key.try_into().ok()?;
    let mask = pin_mask(pin);
    let peer = x25519_dalek::PublicKey::from(xor32(&masked, &mask));
    let secret = x25519_dalek::StaticSecret::from(random_bytes::<32>());
    let public = x25519_dalek::PublicKey::from(&secret);
    let shared = sha256(secret.diffie_hellman(&peer).as_bytes());
    Some((xor32(public.as_bytes(), &mask), shared))
}

/// URL-safe base64 with padding, as the session token's key is written.
pub fn base64url(bytes: &[u8]) -> String {
    const ALPHABET: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    let mut out = String::new();
    for chunk in bytes.chunks(3) {
        let n = (u32::from(chunk[0]) << 16) | (u32::from(*chunk.get(1).unwrap_or(&0)) << 8) | u32::from(*chunk.get(2).unwrap_or(&0));
        for i in 0..4 {
            if i <= chunk.len() {
                out.push(ALPHABET[(n >> (18 - 6 * i) & 63) as usize] as char);
            } else {
                out.push('=');
            }
        }
    }
    out
}

pub fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

pub fn unhex(text: &str) -> Option<Vec<u8>> {
    if text.len() % 2 != 0 {
        return None;
    }
    (0..text.len()).step_by(2).map(|i| u8::from_str_radix(text.get(i..i + 2)?, 16).ok()).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sizes_match_the_captured_ones() {
        // 8 bytes -> 32 (device_token), 16 -> 48 (proof), 70 -> 96 (cert), as on the wire.
        for (plain, sealed) in [(8, 32), (16, 48), (70, 96)] {
            assert_eq!(symmetric_encrypt(&vec![7; plain], &[1; 32]).len(), sealed);
        }
    }

    #[test]
    fn round_trips_and_refuses_the_wrong_key() {
        for key in [&[3u8; 16][..], &[4u8; 32][..]] {
            let sealed = symmetric_encrypt(b"MID0 session text", key);
            assert_eq!(symmetric_decrypt(&sealed, key).unwrap(), b"MID0 session text");
        }
        let sealed = symmetric_encrypt(&42u64.to_le_bytes(), &[5; 32]);
        assert_ne!(symmetric_decrypt(&sealed, &[6; 32]).as_deref(), Some(&42u64.to_le_bytes()[..]));
    }

    #[test]
    fn known_aes_cbc_vector() {
        // NIST SP 800-38A F.2.1 (AES-128-CBC), first block, through the same CBC code.
        let key = unhex("2b7e151628aed2a6abf7158809cf4f3c").unwrap();
        let iv: [u8; 16] = unhex("000102030405060708090a0b0c0d0e0f").unwrap().try_into().unwrap();
        let plain = unhex("6bc1bee22e409f96e93d7e117393172a").unwrap();
        let sealed = symmetric_encrypt_with_iv(&plain, &key, iv);
        assert_eq!(hex(&sealed[16..32]), "7649abac8119b246cee98e9b12e9197d");
    }

    #[test]
    fn both_sides_of_pairing_agree() {
        // The headset's side, as OpenVRLink's probe does it.
        let headset = x25519_dalek::StaticSecret::from([9u8; 32]);
        let headset_public = x25519_dalek::PublicKey::from(&headset);
        let mask = pin_mask("0427");
        let (host_masked, host_shared) = pairing_exchange(&xor32(headset_public.as_bytes(), &mask), "0427").unwrap();
        let host_public = x25519_dalek::PublicKey::from(xor32(&host_masked, &mask));
        assert_eq!(sha256(headset.diffie_hellman(&host_public).as_bytes()), host_shared);
        // A wrong PIN on one side makes another secret.
        let (_, wrong) = pairing_exchange(&xor32(headset_public.as_bytes(), &mask), "0428").unwrap();
        assert_ne!(wrong, host_shared);
    }

    #[test]
    fn base64url_matches_the_standard() {
        assert_eq!(base64url(b""), "");
        assert_eq!(base64url(b"f"), "Zg==");
        assert_eq!(base64url(b"foob"), "Zm9vYg==");
        assert_eq!(base64url(&[0xfb, 0xff]), "-_8=");
        assert_eq!(base64url(&[0u8; 32]).len(), 44);
    }
}
