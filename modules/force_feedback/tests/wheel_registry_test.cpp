#include "descriptors/openffboard.h"
#include "macos/wheel_registry.h"
#include "test.h"

#include <algorithm>

namespace {

using wheel_registry::Choice;
using wheel_registry::Family;

// HID++'s vendor collection: long (0x11, 19 bytes) and very long (0x12, 63 bytes) output reports.
const Bytes kHidpp = {0x06, 0x43, 0xFF, 0x0A, 0x02, 0x06, 0xA1, 0x01, 0x85, 0x11, 0x75, 0x08, 0x95, 0x13,
                      0x15, 0x00, 0x26, 0xFF, 0x00, 0x09, 0x02, 0x91, 0x00, 0x85, 0x12, 0x95, 0x3F, 0x09,
                      0x03, 0x91, 0x00, 0xC0};
// A joystick with one seven-byte vendor output report, ID 1.
const Bytes kOneOutput = {0x05, 0x01, 0x09, 0x04, 0xA1, 0x01, 0x85, 0x01, 0x06, 0x00, 0xFF, 0x09,
                          0x01, 0x75, 0x08, 0x95, 0x07, 0x91, 0x02, 0xC0};

// A vendor collection with one 64-byte output report, ID 1: the direct-drive
// Logitechs' force interface.
const Bytes kStreamForces = {0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x02, 0x15, 0x00, 0x26,
                             0xFF, 0x00, 0x75, 0x08, 0x95, 0x3F, 0x91, 0x02, 0xC0};
// A joystick with a 30-byte input report and no output: the wheel's axes.
const Bytes kJoystickOnly = {0x05, 0x01, 0x09, 0x04, 0xA1, 0x01, 0x09, 0x30, 0x15, 0x00, 0x26,
                             0xFF, 0x00, 0x75, 0x08, 0x95, 0x1E, 0x81, 0x02, 0xC0};

wheel_registry::Device device(std::uint16_t vendor, std::uint16_t product, std::uint16_t version = 0,
                              std::size_t max_output = 0, const Bytes& descriptor = {}) {
    wheel_registry::Device found;
    found.vendor = vendor;
    found.product = product;
    found.version = version;
    found.max_output_report = max_output;
    found.descriptor = ReportDescriptor::parse(descriptor);
    return found;
}

}  // namespace

TEST(registry_known_wheels) {
    const Choice g923 = wheel_registry::choose(device(0x046D, 0xC266, 0x3800, 7));
    CHECK(g923.kind == Choice::Kind::drive);
    CHECK(g923.family == Family::lg4ff);
    CHECK(g923.verified);
    CHECK_EQ(g923.name, std::string("Logitech G923"));
    // An interface that takes no output reports isn't the one for forces.
    CHECK(wheel_registry::choose(device(0x046D, 0xC266, 0x3800, 0)).kind == Choice::Kind::none);
    const Choice g29 = wheel_registry::choose(device(0x046D, 0xC24F, 0x1350, 7));
    CHECK(g29.family == Family::lg4ff);
    CHECK(!g29.verified);

    const Choice g920 = wheel_registry::choose(device(0x046D, 0xC262, 0, 64, kHidpp));
    CHECK(g920.kind == Choice::Kind::drive);
    CHECK(g920.family == Family::hidpp);
    CHECK(wheel_registry::choose(device(0x046D, 0xC26E, 0, 8, kOneOutput)).kind == Choice::Kind::none);

    CHECK(wheel_registry::choose(device(0x044F, 0xB677, 0, 15)).family == Family::t150);
    const Choice t300 = wheel_registry::choose(device(0x044F, 0xB66D, 0, 32));
    CHECK(t300.family == Family::t300);
    CHECK_EQ(t300.known->buffer, std::size_t{31});
    CHECK(wheel_registry::choose(device(0x044F, 0xB696, 0, 64)).known->opening == t300::Opening::two_step);

    const Choice csl = wheel_registry::choose(device(0x0EB7, 0x0E03, 0, 8, kOneOutput));
    CHECK(csl.family == Family::fanatec);
    CHECK(csl.known->high_resolution);
    CHECK(!wheel_registry::choose(device(0x0EB7, 0x0001, 0, 8, kOneOutput)).known->high_resolution);
}

TEST(registry_starting_modes_are_switched) {
    const Choice boot = wheel_registry::choose(device(0x044F, 0xB65D));
    CHECK(boot.kind == Choice::Kind::switch_thrustmaster);
    CHECK(wheel_registry::choose(device(0x044F, 0xB67E)).kind == Choice::Kind::switch_thrustmaster);
    // A G29 in Driving Force mode, named as what it will be.
    const Choice g29 = wheel_registry::choose(device(0x046D, 0xC294, 0x1350, 7));
    CHECK(g29.kind == Choice::Kind::switch_logitech);
    CHECK_EQ(g29.name, std::string("Logitech G29"));
    // A real Driving Force is driven as it is.
    CHECK(wheel_registry::choose(device(0x046D, 0xC294, 0x2100, 7)).kind == Choice::Kind::drive);
    // The G923 in PlayStation mode goes to its own; unrecognised, it's driven as it is.
    CHECK(wheel_registry::choose(device(0x046D, 0xC267, 0x3800, 7)).kind == Choice::Kind::switch_logitech);
    CHECK(wheel_registry::choose(device(0x046D, 0xC267, 0x1200, 7)).kind == Choice::Kind::drive);
}

TEST(registry_hid_pid_by_descriptor) {
    const Bytes openffboard(std::begin(kOpenFfboardDescriptor), std::end(kOpenFfboardDescriptor));
    wheel_registry::Device board = device(0x1209, 0xFFB0, 0, 64, openffboard);
    board.product_name = "FFBoard";
    const Choice choice = wheel_registry::choose(board);
    CHECK(choice.kind == Choice::Kind::drive);
    CHECK(choice.family == Family::pid);
    CHECK_EQ(choice.name, std::string("FFBoard"));
    CHECK(!choice.verified);
    // Known IDs win over a PID descriptor.
    CHECK(wheel_registry::choose(device(0x0EB7, 0x0020, 0, 64, openffboard)).family == Family::fanatec);
    // Something without PID isn't a wheel to drive.
    CHECK(wheel_registry::choose(device(0x1234, 0x5678, 0, 8, kOneOutput)).kind == Choice::Kind::none);
}

TEST(registry_matches_wheels_and_their_starting_modes) {
    const auto products = wheel_registry::products();
    const auto has = [&](std::uint16_t vendor, std::uint16_t product) {
        return std::find(products.begin(), products.end(), std::make_pair(vendor, product)) != products.end();
    };
    CHECK(has(0x046D, 0xC266));
    CHECK(has(0x046D, 0xC294));
    CHECK(has(0x044F, 0xB65D));
    CHECK(has(0x044F, 0xB67E));
    CHECK(has(0x0EB7, 0x0020));
    CHECK(!has(0x046D, 0xC52B));  // a Logitech receiver isn't a wheel
}

TEST(registry_xbox_mode_logitechs_are_switched_over_usb) {
    // usb_modeswitch: the G920 (C261) and the G923 for Xbox (C26D) take
    // 0f 00 01 01 42 on endpoint 0x01 and come back as HID++ at C262 and C26E.
    const auto g920 = wheel_registry::usb_switch(0x046D, 0xC261);
    CHECK(g920.has_value());
    CHECK_EQ(int(g920->endpoint), 0x01);
    CHECK_EQ(g920->message, (Bytes{0x0F, 0x00, 0x01, 0x01, 0x42}));
    CHECK_EQ(g920->native_product, std::uint16_t{0xC262});
    const auto g923 = wheel_registry::usb_switch(0x046D, 0xC26D);
    CHECK_EQ(g923->message, (Bytes{0x0F, 0x00, 0x01, 0x01, 0x42}));
    CHECK_EQ(g923->native_product, std::uint16_t{0xC26E});
    CHECK(wheel_registry::choose(device(0x046D, 0xC26E, 0, 64, kHidpp)).family == Family::hidpp);
    CHECK(!wheel_registry::usb_switch(0x046D, 0xC262).has_value());
    CHECK(!wheel_registry::usb_switch(0x044F, 0xC261).has_value());
    CHECK_EQ(wheel_registry::usb_switch_products().size(), std::size_t{2});
}

TEST(registry_simagic_by_firmware) {
    // Firmware after v159 has no PID descriptor: Simagic's own reports.
    const Choice alpha = wheel_registry::choose(device(0x0483, 0x0522, 0x0171, 64, kOneOutput));
    CHECK(alpha.kind == Choice::Kind::drive);
    CHECK(alpha.family == Family::simagic);
    CHECK(!alpha.verified);
    CHECK(wheel_registry::choose(device(0x3670, 0x0501, 0, 64, kOneOutput)).family == Family::simagic);
    // Up to v159 it still speaks PID.
    const Bytes openffboard(std::begin(kOpenFfboardDescriptor), std::end(kOpenFfboardDescriptor));
    CHECK(wheel_registry::choose(device(0x0483, 0x0522, 0x0159, 64, openffboard)).family == Family::pid);
    // An interface without output reports isn't the one.
    CHECK(wheel_registry::choose(device(0x0483, 0x0522, 0, 0, kJoystickOnly)).kind == Choice::Kind::none);
}

TEST(registry_direct_drive_logitechs) {
    // The G PRO (C268, C272) and RS50 (C276): forces on the interface with
    // output report 0x01, HID++ and the joystick on its others.
    for (const std::uint16_t product : {0xC268, 0xC272, 0xC276}) {
        const Choice forces = wheel_registry::choose(device(0x046D, product, 0, 64, kStreamForces));
        CHECK(forces.kind == Choice::Kind::drive);
        CHECK(forces.family == Family::logitech_stream);
        CHECK(wheel_registry::choose(device(0x046D, product, 0, 64, kHidpp)).kind == Choice::Kind::none);
        CHECK(wheel_registry::choose(device(0x046D, product, 0, 0, kJoystickOnly)).kind == Choice::Kind::none);
    }
    CHECK(wheel_registry::needs_siblings(Family::logitech_stream));
    CHECK(!wheel_registry::needs_siblings(Family::lg4ff));
    CHECK(wheel_registry::sibling(device(0x046D, 0xC276, 0, 64, kHidpp)) == wheel_registry::Sibling::hidpp);
    CHECK(wheel_registry::sibling(device(0x046D, 0xC276, 0, 0, kJoystickOnly)) == wheel_registry::Sibling::input);
    CHECK(wheel_registry::sibling(device(0x046D, 0xC276, 0, 64, kStreamForces)) == wheel_registry::Sibling::none);
}

TEST(registry_prefers_the_wheel_that_steers) {
    // Two wheels with force feedback: the game names the one bound to the
    // steering, and that goes first; the rest follow as they were.
    const std::vector<wheel_registry::Device> devices = {
        device(0x0EB7, 0x0020, 0, 8, kOneOutput),
        device(0x046D, 0xC266, 0x3800, 7),
        device(0x046D, 0xC24F, 0x1350, 7),
    };
    CHECK_EQ(wheel_registry::preferred_order(devices, 0x046D, 0xC266), (std::vector<std::size_t>{1, 0, 2}));
    CHECK_EQ(wheel_registry::preferred_order(devices, 0, 0), (std::vector<std::size_t>{0, 1, 2}));
    // A wheel that isn't there leaves the order as it was.
    CHECK_EQ(wheel_registry::preferred_order(devices, 0x044F, 0xB677), (std::vector<std::size_t>{0, 1, 2}));
}

TEST(registry_families_and_rotation_ranges) {
    CHECK_EQ(std::string(wheel_registry::family_id(Family::lg4ff)), std::string("lg4ff"));
    CHECK_EQ(std::string(wheel_registry::family_id(Family::logitech_stream)), std::string("logitech_stream"));
    // hid-tmff2's per-model range functions.
    const wheel_registry::KnownWheel* t300 = wheel_registry::known_wheel(0x044F, 0xB66E);
    CHECK_EQ(t300->range_least, std::uint16_t{40});
    CHECK_EQ(t300->range, std::uint16_t{1080});
    const wheel_registry::KnownWheel* t248 = wheel_registry::known_wheel(0x044F, 0xB696);
    CHECK_EQ(t248->range_least, std::uint16_t{140});
    CHECK_EQ(t248->range, std::uint16_t{900});
    CHECK_EQ(wheel_registry::known_wheel(0x044F, 0xB67F)->range, std::uint16_t{900});  // TMX
}

TEST(known_wheels_name_their_family_on_every_platform) {
    // Windows plays every wheel through DirectInput but names its family from
    // this table, as its motor's needs are the wheel's own: a G920 there is
    // "hidpp" as on a Mac (as "directinput", its least force went and it never centred).
    CHECK_EQ(std::string(wheel_registry::family_id(wheel_registry::known_wheel(0x046D, 0xC262)->family)), std::string("hidpp"));
    CHECK_EQ(std::string(wheel_registry::family_id(wheel_registry::known_wheel(0x046D, 0xC24F)->family)), std::string("lg4ff"));
    CHECK_EQ(std::string(wheel_registry::family_id(wheel_registry::known_wheel(0x044F, 0xB66E)->family)), std::string("t300"));
    CHECK(wheel_registry::known_wheel(0x1234, 0x5678) == nullptr);
    for (const wheel_registry::KnownWheel& wheel : wheel_registry::known_wheels()) {
        CHECK(std::string(wheel_registry::family_id(wheel.family)) != "");
    }
}


TEST(registry_lg4ff_drives_the_wheels_own_interface_not_a_vendor_one) {
    // A G923 for PlayStation: its joystick interface takes the forces; a
    // vendor interface with a 64-byte output report (G HUB's) is passed by.
    const Choice joystick = wheel_registry::choose(device(0x046D, 0xC266, 0x3800, 16, kOneOutput));
    const Choice vendor = wheel_registry::choose(device(0x046D, 0xC266, 0x3800, 64, kStreamForces));
    CHECK(joystick.kind == Choice::Kind::drive && joystick.family == Family::lg4ff);
    CHECK(vendor.kind == Choice::Kind::none);
}

