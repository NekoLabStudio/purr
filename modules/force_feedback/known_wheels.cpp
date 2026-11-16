#include "known_wheels.h"

#include <iterator>

namespace wheel_registry {

namespace {

using t300::Opening;

// IDs from the kernel's hid-ids.h and hid-lg4ff.c, berarma/new-lg4ff
// (G923), hid-logitech-hidpp.c (G920, G923 for Xbox), Kimplul/hid-tmff2
// (T300 family, and each model's rotation), crossover-wheel (T150),
// her001/tmdrv (TMX), gotzl/hid-fanatecff (Fanatec), JacKeTUs/simagic-ff
// (Simagic) and mescon/logitech-trueforce-linux-driver (G PRO, RS50).
const KnownWheel kWheels[] = {
    {kLogitech, 0xC266, "Logitech G923", Family::lg4ff, true},
    {kLogitech, 0xC267, "Logitech G923", Family::lg4ff, false},  // PlayStation mode
    {kLogitech, 0xC24F, "Logitech G29", Family::lg4ff, false},
    {kLogitech, 0xC29B, "Logitech G27", Family::lg4ff, false},
    {kLogitech, 0xC299, "Logitech G25", Family::lg4ff, false},
    {kLogitech, 0xC29A, "Logitech Driving Force GT", Family::lg4ff, false},
    {kLogitech, 0xC298, "Logitech Driving Force Pro", Family::lg4ff, false},
    {kLogitech, 0xC294, "Logitech Driving Force", Family::lg4ff, false},
    {kLogitech, 0xC295, "Logitech MOMO Force", Family::lg4ff, false},
    {kLogitech, 0xCA03, "Logitech MOMO Racing", Family::lg4ff, false},
    {kLogitech, 0xC262, "Logitech G920", Family::hidpp, false},
    {kLogitech, 0xC26E, "Logitech G923", Family::hidpp, false},  // for Xbox
    {kLogitech, 0xC268, "Logitech G PRO", Family::logitech_stream, false},  // PlayStation and PC
    {kLogitech, 0xC272, "Logitech G PRO", Family::logitech_stream, false},  // Xbox and PC, and an RS50 in G PRO mode
    {kLogitech, 0xC276, "Logitech RS50", Family::logitech_stream, false},
    {kThrustmaster, 0xB677, "Thrustmaster T150", Family::t150, false, 0, Opening::t300rs, false, 1080, 270},
    {kThrustmaster, 0xB67F, "Thrustmaster TMX", Family::t150, false, 0, Opening::t300rs, false, 900, 270},
    {kThrustmaster, 0xB66E, "Thrustmaster T300RS", Family::t300, false, t300::kBuffer, Opening::t300rs, false, 1080, 40},
    {kThrustmaster, 0xB66F, "Thrustmaster T300RS", Family::t300, false, t300::kBuffer, Opening::t300rs, false, 1080, 40},
    {kThrustmaster, 0xB66D, "Thrustmaster T300RS", Family::t300, false, t300::kPs4Buffer, Opening::t300rs, false, 1080,
     40},
    {kThrustmaster, 0xB696, "Thrustmaster T248", Family::t300, false, t300::kBuffer, Opening::two_step, false, 900, 140},
    {kThrustmaster, 0xB669, "Thrustmaster TX", Family::t300, false, t300::kBuffer, Opening::two_step, false, 900, 140},
    {kThrustmaster, 0xB692, "Thrustmaster TS-XW", Family::t300, false, t300::kBuffer, Opening::two_step, false, 1080, 140},
    {kThrustmaster, 0xB689, "Thrustmaster TS-PC Racer", Family::t300, false, t300::kBuffer, Opening::two_step, false, 1080,
     140},
    {kFanatec, 0x0E03, "Fanatec CSL Elite", Family::fanatec, false, 0, Opening::t300rs, true, 1090},
    {kFanatec, 0x0005, "Fanatec CSL Elite", Family::fanatec, false, 0, Opening::t300rs, true, 1090},
    {kFanatec, 0x0020, "Fanatec CSL DD", Family::fanatec, false, 0, Opening::t300rs, true, 2530},
    {kFanatec, 0x0001, "Fanatec ClubSport V2", Family::fanatec, false, 0, Opening::t300rs, false, 900},
    {kFanatec, 0x0004, "Fanatec ClubSport V2.5", Family::fanatec, false, 0, Opening::t300rs, false, 900},
    {kFanatec, 0x0006, "Fanatec Podium DD1", Family::fanatec, false, 0, Opening::t300rs, true, 2530},
    {kFanatec, 0x0007, "Fanatec Podium DD2", Family::fanatec, false, 0, Opening::t300rs, true, 2530},
    {kFanatec, 0x0011, "Fanatec CSR Elite", Family::fanatec, false, 0, Opening::t300rs, false, 900},
    {kFanatec, 0x0197, "Fanatec Porsche 911 wheel", Family::fanatec, false, 0, Opening::t300rs, false, 900},
    {kSimagicAlpha, 0x0522, "Simagic Alpha", Family::simagic, false},  // and the Alpha Mini and Alpha U
    {kSimagic, 0x0500, "Simagic EVO", Family::simagic, false},
    {kSimagic, 0x0501, "Simagic EVO", Family::simagic, false},
    {kSimagic, 0x0502, "Simagic EVO", Family::simagic, false},
};

}  // namespace

const char* family_id(Family family) {
    switch (family) {
    case Family::lg4ff:
        return "lg4ff";
    case Family::hidpp:
        return "hidpp";
    case Family::t150:
        return "t150";
    case Family::t300:
        return "t300";
    case Family::fanatec:
        return "fanatec";
    case Family::pid:
        return "pid";
    case Family::simagic:
        return "simagic";
    case Family::logitech_stream:
        return "logitech_stream";
    }
    return "";
}

const char* family_name(Family family) {
    switch (family) {
    case Family::lg4ff:
        return "lg4ff";
    case Family::hidpp:
        return "Logitech HID++";
    case Family::t150:
        return "Thrustmaster T150";
    case Family::t300:
        return "Thrustmaster T300";
    case Family::fanatec:
        return "Fanatec";
    case Family::pid:
        return "HID PID";
    case Family::simagic:
        return "Simagic";
    case Family::logitech_stream:
        return "Logitech direct drive";
    }
    return "";
}

const KnownWheel* known_wheel(std::uint16_t vendor, std::uint16_t product) {
    for (const KnownWheel& wheel : kWheels) {
        if (wheel.vendor == vendor && wheel.product == product) {
            return &wheel;
        }
    }
    return nullptr;
}

std::vector<KnownWheel> known_wheels() {
    return {std::begin(kWheels), std::end(kWheels)};
}

}  // namespace wheel_registry
