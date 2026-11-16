#pragma once

// Logitech's classic wheels (lg4ff): the G923 for PlayStation, G29, G27,
// G25, Driving Force GT, Driving Force Pro, Driving Force / Formula Force
// EX and MOMO. Every command is a seven-byte unnumbered output report.
//
// Facts from the kernel's drivers/hid/hid-lg4ff.c and hid-ids.h, and
// berarma/new-lg4ff's hid-lg4ff.c, whose effects this follows
// (lg4ff_update_slot, lg4ff_init_slots, lg4ff_devices):
// - Each effect has a slot: the constant force 0, the damper 2, friction
//   3. Byte 0 is 0x10 << slot plus an operation: 1 downloads and plays,
//   0x0C refreshes what's playing, 3 stops it.
// - The constant force is downloaded once as the wheel opens, at nothing
//   (0x80), then only refreshed: it's never stopped while the wheel is
//   held, so the force crossing zero never stops and restarts it.
// - The damper (0x0C) and friction (0x0E) scale their coefficients as
//   SCALE_COEFF does (4 bits and 8), with the sign bytes 0, resisting, and
//   their clip full; each stops at nothing and downloads as it starts.
// - Wheels without a friction effect (the G29, G923, Driving Force and
//   MOMO, by lg4ff_devices' capabilities) play friction as a damper in its
//   slot, as new-lg4ff and Logitech's Windows driver do.
// - Rev lights (lg4ff_set_leds, F8 12): five LEDs as a bitmask, on the G27,
//   G29 and G923.
// - Rotation: lg4ff_set_range_g25 (F8 81) from the G25 on, and
//   lg4ff_set_range_dfp's two commands on the Driving Force Pro, from 40
//   to 900 degrees; the Driving Force and MOMO can't set theirs. A wheel's
//   range can't be read, so a wheel whose range was set goes back to 900,
//   where new-lg4ff starts every wheel, as it's let go.

#include "../wheel_driver.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace lg4ff {

constexpr std::size_t kCommandLength = 7;

// The operations, in byte 0's low nibble.
constexpr std::uint8_t kDownload = 0x01;
constexpr std::uint8_t kStop = 0x03;
constexpr std::uint8_t kRefresh = 0x0C;

constexpr int kConstantSlot = 0;
constexpr int kDamperSlot = 2;
constexpr int kFrictionSlot = 3;

constexpr int kRangeLeast = 40;
constexpr int kRangeMost = 900;

// level -127 to 127: 0x80 is no force, above it turns the wheel to the left
// (measured on a G923).
Bytes constant_force(std::uint8_t op, int level);
// A damper in `slot`, coefficient 0 to 32767 as a Linux condition's.
Bytes damper(int slot, std::uint8_t op, int coefficient);
// Friction, in its slot, coefficient 0 to 32767.
Bytes friction(std::uint8_t op, int coefficient);
Bytes stop_all();
Bytes autocentre_off();
// The Formula Force EX takes its own autocentre command (lg4ff_set_autocenter_ffex).
Bytes autocentre_off_formula_force_ex();
// lg4ff_set_range_g25 and lg4ff_set_range_dfp, for `degrees` already in range.
Bytes range(int degrees);
std::vector<Bytes> range_driving_force_pro(int degrees);
// The rev lights, five bits.
Bytes leds(std::uint8_t mask);
constexpr std::uint32_t kLedMask = 0x1F;

// What a wheel can do beyond the constant force, by lg4ff_devices.
struct Model {
    bool friction = false;
    enum class Range { none, g25, driving_force_pro } range = Range::none;
    bool leds = false;
};
Model model(std::uint16_t product);

// Whether a Driving Force / Formula Force EX (C294) is really the Formula
// Force EX, which the kernel tells by its release number.
bool is_formula_force_ex(std::uint16_t product, std::uint16_t version);

// A wheel that has started in a compatibility mode and the commands that
// switch it to its own (native) mode, after which it re-enumerates as
// `native_product`.
struct ModeSwitch {
    std::uint8_t report_id = 0;
    std::vector<Bytes> commands;
    std::uint16_t native_product = 0;
};
std::optional<ModeSwitch> native_mode_switch(std::uint16_t product, std::uint16_t version);

}  // namespace lg4ff

class Lg4ffDriver : public WheelDriver {
public:
    Lg4ffDriver(Transport& transport, std::uint16_t product, bool formula_force_ex)
        : transport_(transport), model_(lg4ff::model(product)), formula_force_ex_(formula_force_ex) {}

    bool open() override;
    bool set_force(double force) override;
    bool set_damper(double damper) override;
    bool set_friction(double friction) override;
    // new-lg4ff's timer runs every 2 ms (DEFAULT_TIMER_PERIOD).
    int max_update_hz() const override { return 500; }
    int set_rotation(int degrees) override;
    int rotation() const override { return rotation_; }
    bool set_leds(std::uint32_t leds) override;
    void stop() override;
    void close() override;

private:
    bool send(const Bytes& command) { return transport_.send(ReportType::output, 0, command); }
    // A condition slot to play `share` (0 to 1) of its strongest: stopped at
    // nothing, downloaded as it starts, refreshed as it changes.
    bool update(Bytes& sent, int slot, double share, bool as_friction);
    bool apply_range(int degrees);

    Transport& transport_;
    lg4ff::Model model_;
    bool formula_force_ex_;
    bool constant_playing_ = false;
    int sent_force_ = 0;
    // What each condition slot last sent, a download counted as the
    // refresh it becomes (lg4ff_update_slot's comparison).
    Bytes sent_damper_;
    Bytes sent_friction_;
    int rotation_ = 0;
    bool rotation_changed_ = false;
    std::optional<std::uint8_t> sent_leds_;
};
