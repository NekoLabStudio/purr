// HID++ ForceFeedback (0x8123): hid-logitech-hidpp.c's hidpp_ff_* and
// g920_* functions, with the G923 for Xbox's quirks from g923-mac-ffb.

#include "drivers/logitech_hidpp/hidpp.h"
#include "recording_transport.h"
#include "test.h"

namespace {

constexpr std::uint8_t kFeature = 0x0B;  // where a G923 for Xbox has it (g923-mac-ffb)
// Its very long report, 0x12: 63 bytes and the ID (g923-mac-ffb's hidpp-protocol.md).
constexpr std::size_t kVeryLong = 64;
// A release number (bcdDevice) whose low byte hidpp_ff_init reads as
// version 2, which has friction; and one it reads as version 1, which hasn't.
constexpr std::uint16_t kRelease = 0x0002;
constexpr std::uint16_t kFirstRelease = 0x0001;

// A wheel answering every request well, on report 0x12 as the G923 for
// Xbox does: GetFeature with the feature's index, GetInfo with 64 slots,
// GetAperture with 540 degrees, each download with the next free slot.
struct AnsweringWheel : RecordingTransport {
    std::uint8_t next_slot = 1;

    AnsweringWheel() {
        answer = [this](const Bytes& request) -> std::vector<Bytes> {
            Bytes reply = padded({0x12, 0xFF, request[2], request[3]}, 64);
            if (request[2] == 0x00) {
                reply[4] = kFeature;
            } else if (request[3] >> 4 == 0) {
                reply[4] = 64;
            } else if (request[3] >> 4 == 5) {
                reply[4] = 0x02;
                reply[5] = 0x1C;
            } else if (request[3] >> 4 == 2 && request[4] == 0) {
                reply[4] = next_slot++;
            }
            return {reply};
        };
    }
};

}  // namespace

TEST(hidpp_open) {
    AnsweringWheel wheel;
    HidppDriver driver(wheel, kVeryLong, kRelease);
    CHECK(driver.open());
    const std::vector<Bytes> sent = wheel.reports();
    CHECK_EQ(sent.size(), std::size_t{12});
    // Root.GetFeature(0x8123), long report, device 0xFF, function 0 with the kernel's software ID 1.
    CHECK_EQ(sent[0], padded({0x11, 0xFF, 0x00, 0x01, 0x81, 0x23}, 20));
    CHECK_EQ(sent[1], padded({0x11, 0xFF, kFeature, 0x01}, 20));  // GetInfo
    CHECK_EQ(sent[2], padded({0x11, 0xFF, kFeature, 0x11}, 20));  // ResetAll
    // GetAperture (HIDPP_FF_GET_APERTURE), as g920_get_config reads the rotation.
    CHECK_EQ(sent[3], padded({0x11, 0xFF, kFeature, 0x51}, 20));
    CHECK_EQ(sent[4], padded({0x11, 0xFF, kFeature, 0x81, 0xFF, 0xFF, 0x00, 0x00}, 20));  // SetGlobalGains full
    // g920_ff_set_autocenter: a spring of nothing, autostarted. Its 18
    // parameters need a very long report, as hidpp_send_fap_command_sync sends them.
    CHECK_EQ(sent[5], padded({0x12, 0xFF, kFeature, 0x21, 0x00, 0x86}, kVeryLong));
    // The constant force in a slot of the wheel's choosing, autostarted
    // (the G923 for Xbox ignores PLAY otherwise).
    CHECK_EQ(sent[6], padded({0x11, 0xFF, kFeature, 0x21, 0x00, 0x80}, 20));
    // The damper and friction (HIDPP_FF_EFFECT_FRICTION, 0x08), at no strength.
    CHECK_EQ(sent[7], padded({0x12, 0xFF, kFeature, 0x21, 0x00, 0x87}, kVeryLong));
    CHECK_EQ(sent[8], padded({0x12, 0xFF, kFeature, 0x21, 0x00, 0x88}, kVeryLong));
    // SetEffectState(PLAY) for each, as the kernel plays a G920's effects.
    CHECK_EQ(sent[9], padded({0x11, 0xFF, kFeature, 0x31, 0x02, 0x02}, 20));
    CHECK_EQ(sent[10], padded({0x11, 0xFF, kFeature, 0x31, 0x03, 0x02}, 20));
    CHECK_EQ(sent[11], padded({0x11, 0xFF, kFeature, 0x31, 0x04, 0x02}, 20));
    CHECK_EQ(driver.rotation(), 540);
}

TEST(hidpp_force_each_way) {
    AnsweringWheel wheel;
    HidppDriver driver(wheel, kVeryLong, kRelease);
    driver.open();
    const std::size_t from = wheel.sent.size();
    // A positive force on the wire turns the wheel to the left: measured on a
    // G923 for Xbox by g923-mac-ffb. The kernel (G920) sends a Linux level
    // projected through sin(direction), 0x4000 for a wheel, so a game's
    // positive DirectInput force reaches the wire positive there too.
    CHECK(driver.set_force(1.0));
    CHECK(driver.set_force(1.0));
    CHECK(driver.set_force(-1.0));
    const std::vector<Bytes> sent = wheel.reports(from);
    CHECK_EQ(sent.size(), std::size_t{2});
    // DownloadEffect into the constant's own slot (2), changing it in place.
    CHECK_EQ(sent[0], padded({0x11, 0xFF, kFeature, 0x21, 0x02, 0x80, 0x00, 0x00, 0x00, 0x00, 0x7F, 0xFF}, 20));
    CHECK_EQ(sent[1], padded({0x11, 0xFF, kFeature, 0x21, 0x02, 0x80, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01}, 20));
}

TEST(hidpp_damper_and_stop) {
    AnsweringWheel wheel;
    HidppDriver driver(wheel, kVeryLong, kRelease);
    driver.open();
    const std::size_t from = wheel.sent.size();
    CHECK(driver.set_damper(1.0));
    driver.stop();
    const std::vector<Bytes> sent = wheel.reports(from);
    CHECK_EQ(sent.size(), std::size_t{2});
    // hidpp_ff_upload_effect's condition layout: left saturation, left
    // coefficient, dead band, centre, right coefficient, right saturation.
    CHECK_EQ(sent[0], padded({0x12, 0xFF, kFeature, 0x21, 0x03, 0x87, 0x00, 0x00, 0x00, 0x00, 0x7F, 0xFF, 0x7F, 0xFF,
                              0x00, 0x00, 0x00, 0x00, 0x7F, 0xFF, 0x7F, 0xFF},
                             kVeryLong));
    CHECK_EQ(sent[1], padded({0x12, 0xFF, kFeature, 0x21, 0x03, 0x87}, kVeryLong));
}

TEST(hidpp_without_the_feature_doesnt_open) {
    RecordingTransport wheel;
    wheel.answer = [](const Bytes& request) -> std::vector<Bytes> {
        return {padded({0x11, 0xFF, request[2], request[3], 0x00}, 20)};  // feature index 0: not there
    };
    HidppDriver driver(wheel, kVeryLong, kRelease);
    CHECK(!driver.open());
}

TEST(hidpp_error_reply_fails_the_call) {
    // An HID++ 2.0 error: 0xFF, then the request's feature index and function, then the code.
    RecordingTransport wheel;
    wheel.answer = [](const Bytes& request) -> std::vector<Bytes> {
        return {padded({0x11, 0xFF, 0xFF, request[2], request[3], 0x09}, 20)};
    };
    HidppDriver driver(wheel, kVeryLong, kRelease);
    CHECK(!driver.open());
}

TEST(hidpp_skips_other_input_while_waiting) {
    AnsweringWheel wheel;
    wheel.queue_input(padded({0x01, 0x80, 0x00}, 12));  // a joystick report on the same device
    HidppDriver driver(wheel, kVeryLong, kRelease);
    CHECK(driver.open());
}

TEST(hidpp_without_a_very_long_report_plays_no_damper) {
    AnsweringWheel wheel;
    HidppDriver driver(wheel, 0, kRelease);
    CHECK(driver.open());
    const std::size_t from = wheel.sent.size();
    CHECK(driver.set_damper(1.0));
    CHECK_EQ(wheel.sent.size(), from);
    CHECK(driver.set_force(0.5));
    CHECK_EQ(wheel.sent.size(), from + 1);
}

TEST(hidpp_friction) {
    AnsweringWheel wheel;
    HidppDriver driver(wheel, kVeryLong, kRelease);
    driver.open();
    const std::size_t from = wheel.sent.size();
    CHECK(driver.set_friction(1.0));
    CHECK(driver.set_friction(1.0));
    // The damper's condition layout, type 0x08, in friction's own slot (4).
    CHECK_EQ(wheel.reports(from),
             (std::vector<Bytes>{padded({0x12, 0xFF, kFeature, 0x21, 0x04, 0x88, 0x00, 0x00, 0x00, 0x00, 0x7F, 0xFF,
                                         0x7F, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x7F, 0xFF, 0x7F, 0xFF},
                                        kVeryLong)}));
}

TEST(hidpp_plays_one_update_a_tick) {
    // The kernel waits for each answer; this doesn't, so it sends one update
    // a tick at 250 a second, each effect's latest taking its turn.
    AnsweringWheel wheel;
    HidppDriver driver(wheel, kVeryLong, kRelease);
    driver.open();
    CHECK_EQ(driver.max_update_hz(), 250);
    const std::size_t from = wheel.sent.size();
    CHECK(driver.play(0.5, 0.5, 0.5));
    CHECK(driver.play(0.6, 0.5, 0.5));
    CHECK(driver.play(0.7, 0.5, 0.5));
    CHECK(driver.play(0.8, 0.5, 0.5));
    CHECK(driver.play(0.8, 0.5, 0.5));  // all sent: nothing
    const std::vector<Bytes> sent = wheel.reports(from);
    CHECK_EQ(sent.size(), std::size_t{4});
    CHECK_EQ(sent[0][4], std::uint8_t{0x02});  // the force, 0.5
    CHECK_EQ(sent[0][10], std::uint8_t{0x40});
    CHECK_EQ(sent[1][4], std::uint8_t{0x03});  // then the damper
    CHECK_EQ(sent[2][4], std::uint8_t{0x04});  // then friction
    CHECK_EQ(sent[3][4], std::uint8_t{0x02});  // the force's latest, 0.8: 0.6 and 0.7 never went
    CHECK_EQ(sent[3][10], std::uint8_t{0x66});
}

TEST(hidpp_rotation) {
    // SetAperture (HIDPP_FF_SET_APERTURE, function 6), big-endian degrees
    // from 180 to 900 (hidpp_ff_range_store); the 540 read at open goes back on close.
    AnsweringWheel wheel;
    HidppDriver driver(wheel, kVeryLong, kRelease);
    driver.open();
    std::size_t from = wheel.sent.size();
    CHECK_EQ(driver.set_rotation(1080), 900);
    CHECK_EQ(driver.set_rotation(100), 180);
    CHECK_EQ(wheel.reports(from), (std::vector<Bytes>{
                                      padded({0x11, 0xFF, kFeature, 0x61, 0x03, 0x84}, 20),
                                      padded({0x11, 0xFF, kFeature, 0x61, 0x00, 0xB4}, 20),
                                  }));
    from = wheel.sent.size();
    driver.close();
    const std::vector<Bytes> closing = wheel.reports(from);
    CHECK_EQ(closing.size(), std::size_t{2});
    CHECK_EQ(closing[0], padded({0x11, 0xFF, kFeature, 0x61, 0x02, 0x1C}, 20));
    CHECK_EQ(closing[1], padded({0x11, 0xFF, kFeature, 0x11}, 20));  // ResetAll
}

TEST(hidpp_friction_only_from_version_2) {
    // hidpp_ff_init gives a wheel friction only when its release's low byte
    // is past 1: a first version is never sent it, and still opens.
    AnsweringWheel wheel;
    HidppDriver driver(wheel, kVeryLong, kFirstRelease);
    CHECK(driver.open());
    for (const Bytes& report : wheel.reports()) {
        CHECK(!(report[3] == 0x21 && report[5] == 0x88));  // no friction downloaded
    }
    const std::size_t from = wheel.sent.size();
    CHECK(driver.set_friction(1.0));
    CHECK(driver.play(0.0, 0.0, 1.0));
    CHECK_EQ(wheel.sent.size(), from);
}

TEST(hidpp_plays_on_without_friction_it_wont_take) {
    // A wheel answering friction's download with an error opens without it.
    AnsweringWheel wheel;
    const auto answers = wheel.answer;
    wheel.answer = [answers](const Bytes& request) -> std::vector<Bytes> {
        if (request[3] >> 4 == 2 && request[5] == 0x88) {
            return {padded({0x11, 0xFF, 0xFF, request[2], request[3], 0x05}, 20)};
        }
        return answers(request);
    };
    HidppDriver driver(wheel, kVeryLong, kRelease);
    CHECK(driver.open());
    const std::size_t from = wheel.sent.size();
    CHECK(driver.set_friction(1.0));
    CHECK_EQ(wheel.sent.size(), from);
    CHECK(driver.set_force(0.5));
    CHECK_EQ(wheel.sent.size(), from + 1);
}

TEST(hidpp_failed_start_gives_the_wheel_back) {
    // Reset and its centring taken off, then the constant force refused: the
    // wheel is reset again (ResetAll), its own centring back, and not opened.
    AnsweringWheel wheel;
    const auto answers = wheel.answer;
    wheel.answer = [answers](const Bytes& request) -> std::vector<Bytes> {
        if (request[3] >> 4 == 2 && request[5] == 0x80) {
            return {padded({0x11, 0xFF, 0xFF, request[2], request[3], 0x05}, 20)};
        }
        return answers(request);
    };
    HidppDriver driver(wheel, kVeryLong, kRelease);
    CHECK(!driver.open());
    CHECK_EQ(wheel.reports().back(), padded({0x11, 0xFF, kFeature, 0x11}, 20));
}
