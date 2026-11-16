#pragma once

// Taking a Thrustmaster wheel out of the mode it starts in. Most T-series
// wheels come up as a plain gamepad at 044F:B65D; a vendor control transfer
// on endpoint 0 switches each to its own product ID, after which it
// re-enumerates. This happens after every replug, sleep and wake.
//
// At B65D the wheel is asked what it is first (bmRequestType 0xC1, bRequest
// 73, 16 bytes back), then switched (bmRequestType 0x41, bRequest 83,
// wValue the switch value, no data). The TMX and the TX start elsewhere and
// switch in two steps, passing through B65D.
//
// Facts from the kernel's drivers/hid/hid-thrustmaster.c (the model table),
// her001/tmdrv (the TMX, TX and TS-XW steps) and renaudallard/crossover-wheel
// (docs/PROTOCOL.md, src/mac/bootswitch.c), which does this on macOS without
// root. The kernel also sends five packets on the interrupt OUT endpoint
// first; on macOS that pipe can only be reached by capturing the device
// (root, and the steering taken from the game), so they're left out, as
// crossover-wheel's tested switch leaves them out.

#include "../wheel_driver.h"

#include <cstdint>
#include <optional>

namespace thrustmaster {

constexpr std::uint16_t kVendor = 0x044F;
constexpr std::uint16_t kBoot = 0xB65D;
constexpr std::uint16_t kTmxBoot = 0xB67E;
constexpr std::uint16_t kTxBoot = 0xB664;
constexpr std::uint16_t kTsxwBoot = 0xB691;

constexpr std::uint8_t kModelRequestType = 0xC1;
constexpr std::uint8_t kModelRequest = 73;
constexpr std::uint16_t kModelLength = 16;
constexpr std::uint8_t kSwitchRequestType = 0x41;
constexpr std::uint8_t kSwitchRequest = 83;

// The products wheels start as, to be switched rather than driven.
constexpr std::uint16_t kBootProducts[] = {kBoot, kTmxBoot, kTxBoot, kTsxwBoot};
bool is_boot(std::uint16_t product);
// The switch value for a wheel starting at `product` other than B65D.
std::optional<std::uint16_t> first_step(std::uint16_t product);
// The switch value at B65D for the model the wheel gave, or for the wheel
// whose first step was taken from `came_from` (0 if none).
std::optional<std::uint16_t> switch_at_boot(const Bytes& model_reply, std::uint16_t came_from);
// The model reply's wheel type, as hid-thrustmaster.c reads it: the
// little-endian 16 bits at byte 6 (byte 6 the attachment, byte 7 the model).
std::uint16_t wheel_type(const Bytes& model_reply);

}  // namespace thrustmaster
