#pragma once

// Logitech's direct-drive wheels: the G PRO (046D:C268 for PlayStation and
// PC, C272 for Xbox and PC, which the RS50 also shows as in its G PRO
// mode) and the RS50 (C276). Not HID++ 0x8123: the force streams to its
// own interface as output report 0x01, a thousand times a second, and the
// wheel holds nothing between packets. Other settings go through HID++ on
// another interface of the same wheel, and the wheel's position comes in
// on a third.
//
// Facts from mescon/logitech-trueforce-linux-driver (formerly
// logitech-rs50-linux-driver), docs/PROTOCOL_SPECIFICATION.md, written here
// from them:
// - The force packet: 64 bytes, [0x01, 0, 0, 0, 0x01 (a stream packet), a
//   sequence counter wrapping at 0xFF, the force little-endian, the force
//   again, 0 (no new samples), zeros]. The force is offset binary: 0x8000
//   is nothing, 0x0000 the most to the left, 0xFFFF the most to the right.
// - HID++ goes as short reports (0x10: report ID, device 0xFF, feature
//   index, function << 4 | software ID, three parameter bytes) and is
//   answered on the very long report (0x12). Software ID 0x0A, as the
//   driver uses: the pedals' sub-device drops 0x01.
// - Rotation is feature 0x8138: function 1 reads it, function 2 sets it,
//   big-endian degrees from 90 to 2700. In its onboard modes the wheel
//   ignores what the host sets, so the rotation is read back after setting.
// - The wheel has no condition effects: the damper and friction are worked
//   out here from its position, the little-endian 16 bits at bytes 4 and 5
//   of its 30-byte joystick report (0 full left, 0x8000 the centre, 0xFFFF
//   full right, across its rotation).

#include "../wheel_driver.h"

#include <chrono>
#include <cstdint>
#include <functional>

namespace logitech_stream {

using Clock = std::chrono::steady_clock;

constexpr std::uint8_t kForceReport = 0x01;
constexpr std::size_t kForceLength = 64;
constexpr std::uint8_t kShortReport = 0x10;
constexpr std::uint16_t kRotationFeature = 0x8138;
constexpr int kRangeLeast = 90;
constexpr int kRangeMost = 2700;
// A full damper pushes back with full force at this speed (degrees a
// second), and friction reaches its full force by this one.
constexpr double kDamperFullSpeed = 1000.0;
constexpr double kFrictionFullSpeed = 30.0;
// The wheel's speed is smoothed over this long (seconds): its position
// steps a count at a time.
constexpr double kSpeedSmoothing = 0.008;

// What reaches the wire for a force: -1 to 1, positive to the left.
std::uint16_t force_value(double force);
Bytes force_packet(std::uint8_t sequence, double force);
// A short HID++ request.
Bytes request(std::uint8_t feature_index, std::uint8_t function, const Bytes& params);
// The wheel's position from its joystick report, -1 (full left) to 1.
std::optional<double> position(const Bytes& report);

}  // namespace logitech_stream

class LogitechStreamDriver : public WheelDriver {
public:
    // `forces` is the force interface; `hidpp` the HID++ one and `input` the
    // joystick one, either missing (nullptr) if they weren't found.
    LogitechStreamDriver(Transport& forces, Transport* hidpp, Transport* input,
                         std::function<logitech_stream::Clock::time_point()> clock = logitech_stream::Clock::now)
        : forces_(forces), hidpp_(hidpp), input_(input), clock_(std::move(clock)) {}

    bool open() override;
    bool set_force(double force) override;
    bool set_damper(double damper) override;
    bool set_friction(double friction) override;
    // Every tick, changed or not: the wheel keeps nothing between packets.
    bool play(double force, double damper, double friction) override;
    // Games stream it at 1 kHz.
    int max_update_hz() const override { return 1000; }
    int set_rotation(int degrees) override;
    int rotation() const override { return rotation_; }
    void stop() override;
    void close() override;

private:
    bool send_force(double force);
    std::optional<Bytes> call(std::uint8_t feature_index, std::uint8_t function, const Bytes& params);
    std::optional<int> read_rotation();
    // The wheel's latest position, and its speed in degrees a second.
    void follow_wheel();

    Transport& forces_;
    Transport* hidpp_;
    Transport* input_;
    std::function<logitech_stream::Clock::time_point()> clock_;
    std::uint8_t sequence_ = 0;
    double force_ = 0.0;
    double damper_ = 0.0;
    double friction_ = 0.0;
    std::uint8_t rotation_feature_ = 0;
    int rotation_ = 0;
    int rotation_before_ = 0;
    bool rotation_changed_ = false;
    std::optional<double> angle_;  // degrees, right positive
    logitech_stream::Clock::time_point angle_at_{};
    double speed_ = 0.0;  // degrees a second, right positive
};
