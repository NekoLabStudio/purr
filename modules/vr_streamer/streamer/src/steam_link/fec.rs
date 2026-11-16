//! The video's error correction (OpenVRLink's docs/svl-video/fec.md): a RAID-6 code over stripes
//! of eight 1024-byte data packets, jerasure's Liber8tion, XOR only. Each packet is eight 128-byte
//! sub-blocks; P is the XOR of the eight packets, and each sub-block of Q the XOR of one sub-block
//! of each packet (Q_PERM) plus one extra for packets 1 to 7 (Q_EXTRA). With P and Q a stripe
//! survives any two losses.

pub const PACKET: usize = 1024;
pub const DATA_PER_STRIPE: usize = 8;
const SUB_BLOCKS: usize = 8;
const SUB_BLOCK: usize = 128;

/// Q_PERM[j][r]: the sub-block of data packet j that feeds Q's sub-block r.
const Q_PERM: [[usize; 8]; 8] = [
    [0, 1, 2, 3, 4, 5, 6, 7],
    [7, 3, 0, 2, 6, 1, 5, 4],
    [6, 2, 4, 0, 7, 3, 1, 5],
    [2, 5, 7, 6, 0, 3, 4, 1],
    [5, 6, 1, 7, 2, 4, 3, 0],
    [1, 2, 3, 4, 5, 6, 7, 0],
    [3, 0, 6, 5, 1, 7, 4, 2],
    [4, 7, 1, 5, 3, 2, 0, 6],
];
/// Packet j's extra term: Q's sub-block `.0` also takes its sub-block `.1`.
const Q_EXTRA: [(usize, (usize, usize)); 7] = [(1, (4, 7)), (2, (1, 3)), (3, (5, 4)), (4, (2, 0)), (5, (7, 2)), (6, (6, 5)), (7, (3, 1))];

fn xor_into(target: &mut [u8], source: &[u8]) {
    for (t, s) in target.iter_mut().zip(source) {
        *t ^= s;
    }
}

fn sub_block(packet: &[u8], index: usize) -> &[u8] {
    &packet[index * SUB_BLOCK..(index + 1) * SUB_BLOCK]
}

/// P and Q for one stripe: eight full-size packets (a frame's last ones zero-padded, and packets
/// past its end all zeros).
pub fn encode(stripe: &[&[u8]; DATA_PER_STRIPE]) -> ([u8; PACKET], [u8; PACKET]) {
    let mut p = [0u8; PACKET];
    let mut q = [0u8; PACKET];
    for packet in stripe {
        xor_into(&mut p, packet);
    }
    for row in 0..SUB_BLOCKS {
        let target = &mut q[row * SUB_BLOCK..(row + 1) * SUB_BLOCK];
        for (j, packet) in stripe.iter().enumerate() {
            xor_into(target, sub_block(packet, Q_PERM[j][row]));
        }
        for &(j, (extra_row, column)) in &Q_EXTRA {
            if extra_row == row {
                xor_into(target, sub_block(stripe[j], column));
            }
        }
    }
    (p, q)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The parity equations as (packet, sub-block) sets: P's eight, then Q's eight.
    fn equations() -> Vec<Vec<(usize, usize)>> {
        let mut rows: Vec<Vec<(usize, usize)>> = (0..8).map(|c| (0..8).map(|j| (j, c)).collect()).collect();
        for r in 0..8 {
            let mut terms: Vec<(usize, usize)> = (0..8).map(|j| (j, Q_PERM[j][r])).collect();
            for &(j, (er, ec)) in &Q_EXTRA {
                if er == r {
                    if let Some(at) = terms.iter().position(|&t| t == (j, ec)) {
                        terms.remove(at);
                    } else {
                        terms.push((j, ec));
                    }
                }
            }
            rows.push(terms);
        }
        rows
    }

    /// Rebuilds two lost packets from the rest and P and Q, by Gaussian elimination over GF(2).
    fn repair(stripe: &[Vec<u8>], p: &[u8], q: &[u8], lost: [usize; 2]) -> Option<Vec<Vec<u8>>> {
        let unknown = |packet: usize, block: usize| lost.iter().position(|&l| l == packet).map(|i| i * 8 + block);
        let mut system: Vec<(u32, Vec<u8>)> = Vec::new();
        for (row, terms) in equations().iter().enumerate() {
            let parity = if row < 8 { p } else { q };
            let mut value = sub_block(parity, row % 8).to_vec();
            let mut mask = 0u32;
            for &(packet, block) in terms {
                match unknown(packet, block) {
                    Some(u) => mask ^= 1 << u,
                    None => xor_into(&mut value, sub_block(&stripe[packet], block)),
                }
            }
            system.push((mask, value));
        }
        let mut solved = 0;
        for column in 0..16 {
            let bit = 1u32 << column;
            let pivot = (solved..system.len()).find(|&r| system[r].0 & bit != 0)?;
            system.swap(solved, pivot);
            let (pivot_mask, pivot_value) = system[solved].clone();
            for (row, entry) in system.iter_mut().enumerate() {
                if row != solved && entry.0 & bit != 0 {
                    entry.0 ^= pivot_mask;
                    xor_into(&mut entry.1, &pivot_value);
                }
            }
            solved += 1;
        }
        Some((0..2).map(|i| (0..8).flat_map(|b| system[i * 8 + b].1.clone()).collect()).collect())
    }

    #[test]
    fn q_has_the_liber8tion_minimum_of_ones() {
        // k*w + k - 1 = 71 terms in Q's half, as the docs count.
        assert_eq!(equations()[8..].iter().map(Vec::len).sum::<usize>(), 71);
    }

    #[test]
    fn any_two_lost_packets_come_back() {
        let stripe: Vec<Vec<u8>> = (0..8).map(|j| (0..PACKET).map(|i| ((j * PACKET + i) * 31 + 7) as u8).collect()).collect();
        let refs: [&[u8]; 8] = std::array::from_fn(|j| stripe[j].as_slice());
        let (p, q) = encode(&refs);
        for a in 0..8 {
            for b in a + 1..8 {
                let rebuilt = repair(&stripe, &p, &q, [a, b]).unwrap_or_else(|| panic!("packets {a} and {b} unrecoverable"));
                assert_eq!(rebuilt[0], stripe[a]);
                assert_eq!(rebuilt[1], stripe[b]);
            }
        }
        // P alone is the XOR of the stripe.
        let mut xor = vec![0u8; PACKET];
        for packet in &stripe {
            xor_into(&mut xor, packet);
        }
        assert_eq!(p.to_vec(), xor);
    }
}
