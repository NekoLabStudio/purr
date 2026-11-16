#pragma once

// Logitech's HID++ wheels, through the ForceFeedback feature (0x8123): the
// G920 and the G923 for Xbox. Requests go as HID++ long reports (ID 0x11,
// 20 bytes: report ID, device index 0xFF, feature index,
// function << 4 | software ID, 16 bytes of parameters), or very long ones
// (ID 0x12, as long as the device declares it) when the parameters don't
// fit, as a condition effect's 18 don't.
//
// Facts from the kernel's drivers/hid/hid-logitech-hidpp.c (the hidpp_ff_*
// and g920_* functions, which drive both wheels) and the notes of
// CesarOvilla/g923-mac-ffb, measured on a G923 for Xbox: it replies on
// report 0x12 as well as 0x11, ignores SetEffectState(PLAY) unless the
// effect was downloaded with the autostart bit, and a positive force on the
// wire turns it to the left.
//
// The kernel waits for each update's answer before sending the next; this
// sends without waiting, so it's kept to one update a tick of the thread at
// kUpdateHz (play), the latest of each effect taking its turn. Friction is
// its own condition effect (type 0x08, as the damper's layout), only on a
// wheel whose release number's low byte is past 1: hidpp_ff_init gives
// friction only to those (its "version"), so others are never sent it, and
// one that won't take it plays without. Rotation is GetAperture and
// SetAperture, from 180 to 900 degrees (g920_get_config,
// hidpp_ff_range_store); what it was goes back on close. A start that fails
// part way resets the wheel (ResetAll), giving it its own centring back.

#include "../wheel_driver.h"

#include <cstdint>

namespace hidpp {

constexpr std::uint8_t kLongReport = 0x11;
constexpr std::uint8_t kVeryLongReport = 0x12;
constexpr std::size_t kLongLength = 20;
constexpr std::size_t kLongParams = 16;
constexpr std::uint16_t kForceFeedbackFeature = 0x8123;

// A request to function `function` of the feature at `feature_index`: a
// long report, or a very long one of `very_long_length` bytes when the
// parameters need it (empty if the device has none).
Bytes request(std::uint8_t feature_index, std::uint8_t function, const Bytes& params, std::size_t very_long_length);

// The parameters of a constant force (level -32767 to 32767) or a damper
// (coefficient 0 to 32767, both ways) for DownloadEffect, in `slot` (0 has
// the wheel pick one), set to start as they land.
Bytes constant_effect(std::uint8_t slot, int level);
Bytes damper_effect(std::uint8_t slot, int coefficient);
Bytes friction_effect(std::uint8_t slot, int coefficient);
// A spring of no strength, set to start: what the kernel downloads first to
// take the wheel's own centring away (g920_ff_set_autocenter).
Bytes no_spring();

}  // namespace hidpp

class HidppDriver : public WheelDriver {
public:
    // `very_long_length`: the device's very long report (0x12), its ID
    // counted; 0 if it has none. `release`: its release number (bcdDevice).
    HidppDriver(Transport& transport, std::size_t very_long_length, std::uint16_t release)
        : transport_(transport), very_long_length_(very_long_length), has_friction_((release & 0xFF) > 1) {}

    static constexpr int kUpdateHz = 250;
    static constexpr int kRangeLeast = 180;
    static constexpr int kRangeMost = 900;

    bool open() override;
    bool set_force(double force) override;
    bool set_damper(double damper) override;
    bool set_friction(double friction) override;
    // One update a tick: the next effect in turn whose latest isn't sent.
    bool play(double force, double damper, double friction) override;
    int max_update_hz() const override { return kUpdateHz; }
    int set_rotation(int degrees) override;
    int rotation() const override { return rotation_; }
    int errors() const override { return errors_; }
    void stop() override;
    void close() override;

private:
    enum Effect { force, damper, friction, effects };

    // Each effect's latest, in its own steps, and what was sent of it.
    struct Pending {
        int wanted = 0;
        int sent = 0;
    };
    bool send_effect(Effect effect);
    std::optional<std::uint8_t> download(const Bytes& params, const char* what);
    // Downloads and plays the effects, once the wheel's been reset.
    bool start_effects();

    // Sends a request and waits for its answer: its parameters, or nothing
    // if it failed (logged).
    std::optional<Bytes> call(std::uint8_t feature_index, std::uint8_t function, const Bytes& params, const char* what);
    // Sends a request without waiting. Errors that come back are counted.
    bool post(std::uint8_t function, const Bytes& params);
    void count_errors();

    bool send(const Bytes& report);

    Transport& transport_;
    std::size_t very_long_length_;
    bool has_friction_;
    std::uint8_t feature_ = 0;
    std::uint8_t slots_[effects] = {};
    Pending pending_[effects];
    int next_ = 0;
    int errors_ = 0;
    int rotation_ = 0;
    int rotation_before_ = 0;
    bool rotation_changed_ = false;
};
