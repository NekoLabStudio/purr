#pragma once

// The wheels the game knows by vendor and product ID, each with its family
// (what drives it, and what its motor needs: the game's data names it), on
// every platform: macOS picks its driver by it (wheel_registry), Windows
// plays every wheel through DirectInput but names its family all the same,
// as a geared G920's needs (its least force, its friction) are the wheel's,
// not the API's.

#include "drivers/thrustmaster/boot_switch.h"
#include "drivers/thrustmaster/t300.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wheel_registry {

constexpr std::uint16_t kLogitech = 0x046D;
constexpr std::uint16_t kThrustmaster = thrustmaster::kVendor;
constexpr std::uint16_t kFanatec = 0x0EB7;
constexpr std::uint16_t kSimagicAlpha = 0x0483;
constexpr std::uint16_t kSimagic = 0x3670;

enum class Family { lg4ff, hidpp, t150, t300, fanatec, pid, simagic, logitech_stream };

// The family as the game's data names it ("lg4ff", "hidpp", ...), and as
// the log says it.
const char* family_id(Family family);
const char* family_name(Family family);

struct KnownWheel {
    std::uint16_t vendor;
    std::uint16_t product;
    const char* name;
    Family family;
    // Force feedback confirmed on the real thing.
    bool verified;
    // The T300 family's buffer and opening; a Fanatec base's resolution.
    std::size_t buffer = 0;
    t300::Opening opening = t300::Opening::t300rs;
    bool high_resolution = false;
    // Its rotation in degrees where its driver sets it from a table: the
    // most (a Fanatec base's, which its driver sets to turn the base's own
    // centring off; a Thrustmaster's) and the least (Thrustmaster's).
    std::uint16_t range = 0;
    std::uint16_t range_least = 0;
};

// The family as the game's data names it ("lg4ff", "hidpp", ...), and as
// the log says it.
const char* family_id(Family family);
const char* family_name(Family family);

// Every known wheel, in the order they're listed.
std::vector<KnownWheel> known_wheels();

// The known wheel with these IDs, or null.
const KnownWheel* known_wheel(std::uint16_t vendor, std::uint16_t product);

}  // namespace wheel_registry
