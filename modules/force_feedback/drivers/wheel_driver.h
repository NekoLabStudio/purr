#pragma once

// What every wheel driver implements, and the transport it reaches its wheel
// through. Drivers turn the game's normalised force, damper and friction
// into their wheel's own reports, and set its rotation where it has one to
// set; they never touch IOKit, so tests run them against a recorder
// (tests/recording_transport.h).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

using Bytes = std::vector<std::uint8_t>;

enum class ReportType { input, output, feature };

// A wheel's HID reports, as IOKit's IOHIDDevice gives them (hid_device.h).
class Transport {
public:
    virtual ~Transport() = default;
    // Sends a report. When `id` isn't 0 the report starts with it, as
    // IOHIDDeviceSetReport takes it.
    virtual bool send(ReportType type, std::uint8_t id, const Bytes& report) = 0;
    // Reads a feature report of `length` bytes (its ID first when numbered).
    virtual std::optional<Bytes> read_feature(std::uint8_t id, std::size_t length) = 0;
    // The oldest input report not yet read, waiting up to `timeout` for one.
    virtual std::optional<Bytes> next_input(std::chrono::milliseconds timeout) = 0;
};

// One wheel family's protocol. Each remembers what it last sent and skips
// repeats, so the game can set the same force a few hundred times a second.
class WheelDriver {
public:
    virtual ~WheelDriver() = default;
    // The wheel's start: its own centring spring off, its gain full, the
    // force and the damper ready to play. False if the wheel won't take it.
    virtual bool open() = 0;
    // -1 to 1, positive turning the wheel to the left. Each returns false
    // once the wheel stops answering (unplugged).
    virtual bool set_force(double force) = 0;
    // 0 to 1, of the wheel's strongest damper.
    virtual bool set_damper(double damper) = 0;
    // 0 to 1, of the wheel's strongest friction. A wheel without a friction
    // effect plays it as its maker's driver does (a damper), or not at all.
    virtual bool set_friction(double) { return true; }
    // One tick of the thread: everything at once. A wheel that takes only so
    // much a tick sends what it can and the rest on the next.
    virtual bool play(double force, double damper, double friction) {
        return set_force(force) && set_damper(damper) && set_friction(friction);
    }
    // How many updates a second the wheel keeps up with, from its reference
    // driver: the thread plays at this rate.
    virtual int max_update_hz() const { return 400; }
    // The wheel's rotation, lock to lock in degrees: set as near `degrees`
    // as it goes, and what it's set to; 0 where this driver can't set it.
    // Whatever was there before goes back on close.
    virtual int set_rotation(int) { return 0; }
    // Its rotation as last set or read; 0 if unknown.
    virtual int rotation() const { return 0; }
    // The wheel's rev lights, one bit each from the first to light (bit 0) to the last; a wheel
    // without them ignores it. False once the wheel stops answering.
    virtual bool set_leds(std::uint32_t) { return true; }
    // Answers from the wheel that an update failed, since it opened.
    virtual int errors() const { return 0; }
    // No force, no damper.
    virtual void stop() = 0;
    // Stops and hands the wheel back.
    virtual void close() = 0;
};

// A share (-1 to 1, or 0 to 1) as a whole number of a field's steps, clamped
// first so no caller can push a field past its range.
inline int steps(double share, int full, double lowest = -1.0) {
    return static_cast<int>(std::lround(std::clamp(share, lowest, 1.0) * full));
}
