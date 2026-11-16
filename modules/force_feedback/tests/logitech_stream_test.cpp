// Logitech's direct-drive stream (the G PRO and RS50), from
// mescon/logitech-trueforce-linux-driver's PROTOCOL_SPECIFICATION.md:
// section 4 (the force packet and its offset binary), 5 (HID++ short
// requests, 0x8138 rotation) and 3 (the joystick report's position).

#include "drivers/logitech_stream/logitech_stream.h"
#include "recording_transport.h"
#include "test.h"

#include <cmath>

namespace {

using logitech_stream::Clock;

constexpr std::uint8_t kRotationIndex = 0x18;  // where the RS50 has it

// The HID++ interface answering on the very long report, as the RS50 does:
// GetFeature(0x8138) with its index, the rotation's get and set. In an
// onboard mode it keeps its own rotation whatever it's sent.
struct AnsweringHidpp : RecordingTransport {
    int degrees = 1080;
    bool onboard = false;

    AnsweringHidpp() {
        answer = [this](const Bytes& request) -> std::vector<Bytes> {
            Bytes reply = padded({0x12, 0xFF, request[2], request[3]}, 64);
            const int function = request[3] >> 4;
            if (request[2] == 0x00) {
                reply[4] = kRotationIndex;
            } else if (function == 2 && !onboard) {
                degrees = request[4] << 8 | request[5];
            } else if (function == 1) {
                reply[4] = static_cast<std::uint8_t>(degrees >> 8);
                reply[5] = static_cast<std::uint8_t>(degrees & 0xFF);
            }
            return {reply};
        };
    }
};

// A joystick report with the wheel at `raw` (0 full left, 0xFFFF full right).
Bytes joystick(int raw) {
    Bytes report = padded({0x08, 0x00, 0x00, 0x00}, 30);
    report[4] = static_cast<std::uint8_t>(raw & 0xFF);
    report[5] = static_cast<std::uint8_t>(raw >> 8);
    report[18] = 0x01;
    return report;
}

std::uint16_t wire_force(const Bytes& packet) {
    return static_cast<std::uint16_t>(packet[6] | packet[7] << 8);
}

}  // namespace

TEST(stream_force_packet_and_sign) {
    // Offset binary: 0x8000 nothing, 0x0000 the most to the left, so the
    // game's left (positive) is below 0x8000; the force twice, byte 10 0.
    CHECK_EQ(logitech_stream::force_value(0.0), std::uint16_t{0x8000});
    CHECK_EQ(logitech_stream::force_value(1.0), std::uint16_t{0x0001});
    CHECK_EQ(logitech_stream::force_value(-1.0), std::uint16_t{0xFFFF});
    CHECK_EQ(logitech_stream::force_value(0.5), std::uint16_t{0x4000});
    CHECK_EQ(logitech_stream::force_packet(0x7F, 0.0), padded({0x01, 0x00, 0x00, 0x00, 0x01, 0x7F, 0x00, 0x80, 0x00, 0x80}, 64));
    CHECK_EQ(logitech_stream::force_packet(0x00, 1.0), padded({0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00}, 64));
}

TEST(stream_sends_every_tick_and_counts_round) {
    RecordingTransport forces;
    LogitechStreamDriver driver(forces, nullptr, nullptr);
    CHECK(driver.open());  // one packet at nothing, sequence 0
    CHECK_EQ(driver.max_update_hz(), 1000);
    for (int i = 0; i < 300; ++i) {
        CHECK(driver.play(0.25, 0.0, 0.0));  // the same force: sent all the same
    }
    CHECK_EQ(forces.sent.size(), std::size_t{301});
    CHECK_EQ(int(forces.sent[0].id), 0x01);
    CHECK_EQ(forces.sent[255].report[5], std::uint8_t{0xFF});
    CHECK_EQ(forces.sent[256].report[5], std::uint8_t{0x00});  // a single byte, wrapping
    CHECK_EQ(wire_force(forces.sent[300].report), std::uint16_t{0x8000 - 8192});
}

TEST(stream_rotation_through_hidpp) {
    // Feature 0x8138 found through IRoot, read with function 1, set with
    // function 2 as big-endian degrees, short reports with software ID 0x0A.
    RecordingTransport forces;
    AnsweringHidpp hidpp;
    LogitechStreamDriver driver(forces, &hidpp, nullptr);
    CHECK(driver.open());
    CHECK_EQ(driver.rotation(), 1080);
    CHECK_EQ(hidpp.reports(), (std::vector<Bytes>{
                                  {0x10, 0xFF, 0x00, 0x0A, 0x81, 0x38, 0x00},
                                  {0x10, 0xFF, 0x18, 0x1A, 0x00, 0x00, 0x00},
                              }));
    CHECK_EQ(int(hidpp.sent[0].id), 0x10);
    std::size_t from = hidpp.sent.size();
    CHECK_EQ(driver.set_rotation(900), 900);
    CHECK_EQ(driver.set_rotation(9000), 2700);
    CHECK_EQ(driver.set_rotation(10), 90);
    CHECK_EQ(hidpp.reports(from)[0], (Bytes{0x10, 0xFF, 0x18, 0x2A, 0x03, 0x84, 0x00}));
    CHECK_EQ(hidpp.reports(from)[2], (Bytes{0x10, 0xFF, 0x18, 0x2A, 0x0A, 0x8C, 0x00}));
    // Let go, it goes back to what it was.
    from = hidpp.sent.size();
    driver.close();
    CHECK_EQ(hidpp.reports(from), (std::vector<Bytes>{{0x10, 0xFF, 0x18, 0x2A, 0x04, 0x38, 0x00}}));
    CHECK_EQ(hidpp.degrees, 1080);
}

TEST(stream_rotation_kept_by_an_onboard_mode_is_read_back) {
    RecordingTransport forces;
    AnsweringHidpp hidpp;
    hidpp.onboard = true;
    LogitechStreamDriver driver(forces, &hidpp, nullptr);
    driver.open();
    CHECK_EQ(driver.set_rotation(900), 1080);
}

TEST(stream_without_hidpp_cant_set_rotation) {
    RecordingTransport forces;
    LogitechStreamDriver driver(forces, nullptr, nullptr);
    driver.open();
    CHECK_EQ(driver.set_rotation(900), 0);
}

TEST(stream_damper_and_friction_from_the_wheels_position) {
    // No condition effects: worked out from the position in the joystick
    // report. Turned right at 500 degrees a second across 1080 degrees, a
    // full damper pushes left (positive) with half its force; friction full.
    RecordingTransport forces;
    AnsweringHidpp hidpp;
    RecordingTransport input;
    Clock::time_point now{};
    LogitechStreamDriver driver(forces, &hidpp, &input, [&now] { return now; });
    driver.open();
    const double counts_a_tick = 500.0 * 0.001 / 540.0 * 32768.0;
    double raw = 0x8000;
    for (int tick = 0; tick < 100; ++tick) {
        now += std::chrono::milliseconds(1);
        raw += counts_a_tick;
        input.queue_input(joystick(static_cast<int>(std::lround(raw))));
        driver.play(0.0, 1.0, 0.0);
    }
    const double damping = (0x8000 - wire_force(forces.sent.back().report)) / 32767.0;
    CHECK(std::abs(damping - 0.5) < 0.03);
    // Turning left, it pushes right; friction at full by 30 degrees a second.
    for (int tick = 0; tick < 100; ++tick) {
        now += std::chrono::milliseconds(1);
        raw -= counts_a_tick;
        input.queue_input(joystick(static_cast<int>(std::lround(raw))));
        driver.play(0.0, 0.0, 0.4);
    }
    const double rubbing = (0x8000 - wire_force(forces.sent.back().report)) / 32767.0;
    CHECK(std::abs(rubbing + 0.4) < 0.01);
    // Held still (no reports), both fade away.
    for (int tick = 0; tick < 100; ++tick) {
        now += std::chrono::milliseconds(1);
        driver.play(0.0, 1.0, 0.4);
    }
    CHECK(std::abs(wire_force(forces.sent.back().report) - 0x8000) < 40);
}

TEST(stream_rotation_change_follows_the_wheel_afresh) {
    // The same position means other degrees after the rotation changes: no
    // kick from a speed that never was.
    RecordingTransport forces;
    AnsweringHidpp hidpp;
    RecordingTransport input;
    Clock::time_point now{};
    LogitechStreamDriver driver(forces, &hidpp, &input, [&now] { return now; });
    driver.open();
    for (int tick = 0; tick < 20; ++tick) {
        now += std::chrono::milliseconds(1);
        input.queue_input(joystick(0xC000));
        driver.play(0.0, 1.0, 1.0);
    }
    driver.set_rotation(360);
    for (int tick = 0; tick < 5; ++tick) {
        now += std::chrono::milliseconds(1);
        input.queue_input(joystick(0xC000));
        driver.play(0.0, 1.0, 1.0);
        CHECK_EQ(wire_force(forces.sent.back().report), std::uint16_t{0x8000});
    }
}
