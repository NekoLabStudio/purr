#pragma once

// One platform's way of driving a force feedback wheel: what the shared
// SteeringWheel (steering_wheel.h) hands its forces to. Values
// are normalised: a force from -1 to 1 (positive turning the wheel to the
// left, as the game means it), a damper and friction from 0 to 1. Each
// wheel's driver turns them into its own steps and sign.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

struct FoundWheel {
    std::string name;
    // Force feedback for this wheel has been confirmed on the real thing;
    // otherwise the game says it's new.
    bool verified = true;
    // Its USB vendor and product IDs, and its driver's family ("lg4ff",
    // "hidpp", "t150", "t300", "fanatec", "pid", "simagic",
    // "logitech_stream", or "directinput" on Windows), for the game's data
    // on each wheel (its strength).
    std::uint16_t vendor = 0;
    std::uint16_t product = 0;
    std::string family;
};

// What went to the wheel since it opened.
struct OutputCounts {
    std::uint64_t reports = 0;
    // Updates the wheel answered with an error.
    std::uint64_t errors = 0;
};

class WheelOutput {
public:
    virtual ~WheelOutput() = default;

    // The wheel this platform would open, without taking it: the one with
    // these vendor and product IDs when there's one (and they aren't 0), else
    // the first it can drive.
    virtual std::optional<FoundWheel> find(std::uint16_t vendor, std::uint16_t product) = 0;
    // Takes that wheel for force feedback, switching off its own centring
    // spring. `window` is the game's native window handle (Windows needs it
    // to take the wheel); 0 elsewhere.
    virtual bool open(std::uintptr_t window, std::uint16_t vendor, std::uint16_t product) = 0;
    virtual void close() = 0;
    // The wheel it has open.
    virtual std::optional<FoundWheel> wheel() const = 0;
    // Each returns false once the wheel has stopped answering (unplugged).
    virtual bool set_force(double force) = 0;
    virtual bool set_damper(double damper) = 0;
    virtual bool set_friction(double friction) = 0;
    // One tick of the thread: all three, as much as the wheel takes a tick.
    virtual bool play(double force, double damper, double friction) {
        return set_force(force) && set_damper(damper) && set_friction(friction);
    }
    // The wheel's rotation, lock to lock in degrees: set as near `degrees`
    // as it goes, and what it's set to, or 0 where it can't be set from
    // here. What was there before goes back as the wheel is let go.
    virtual int set_rotation(int) { return 0; }
    // As last set or read; 0 if unknown.
    virtual int rotation() const { return 0; }
    // The steering axis as the wheel reports it now, -1 to 1 over its
    // logical range (as the game's joypad input reads the same axis, so a
    // binding's direction holds), read on the thread; nothing where this
    // platform or wheel can't say.
    virtual std::optional<double> position() { return std::nullopt; }
    // How the steering axis moves as a positive force turns the wheel: -1 where it falls (an HID
    // wheel's X rises to the right, and a positive force turns left), 1 where it rises.
    virtual int axis_direction() const { return -1; }
    // The wheel's rev lights (WheelDriver::set_leds); nothing on a wheel without them.
    virtual bool set_leds(std::uint32_t) { return true; }
    // How many updates a second the open wheel keeps up with.
    virtual int update_hz() const { return 400; }
    virtual OutputCounts counts() const { return {}; }
    // Lets the wheel go: no force, no damper, no friction.
    virtual void stop() = 0;
    // Switches any wheel attached in a starting mode to its own, without
    // taking it for force feedback.
    virtual void prepare() {}

    // This platform's output.
    static std::unique_ptr<WheelOutput> create();
    // From now on, switches each wheel out of its starting mode as it's
    // plugged in, before the game binds it (macOS); nothing elsewhere.
    static void watch();
};
