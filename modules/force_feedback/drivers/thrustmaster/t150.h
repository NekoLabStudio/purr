#pragma once

// The Thrustmaster T150 (044F:B677) and TMX (044F:B67F), which share one
// protocol. Every packet goes through IOHIDDeviceSetReport as an unnumbered
// output report of its own length.
//
// Facts from renaudallard/crossover-wheel (docs/PROTOCOL.md and
// include/t150/t150.h, measured on a T150 on macOS and against
// Thrustmaster's own driver), which takes them from scarburato/t150_driver.
// Written here from those facts, not copied.
//
// - The firmware plays nothing until the wheel's input is opened (42 04);
//   it's closed with 42 05 twice, then 42 00.
// - Its own centring only lets go when its force is set to nothing (40 03 00 00).
// - An effect uploads as three packets (ff_first, ff_update, ff_commit) and
//   plays on a control packet; after that a change is a bare ff_update.
// - A constant force's full scale is 64: Thrustmaster's driver halves it on
//   this wheel, and its captures step 0x40, 0x20, 0x10 for 100, 50 and 25%.
// - Its damper buzzes at the top of its range, so the coefficient stops at
//   80 of 100, as crossover-wheel measured.
// - It has no friction effect: friction is added to the damper, as
//   crossover-wheel downgrades it.
// - Rotation is the settings packet 40 11, degrees * 0xFFFF / 1080
//   (t150_driver, which clamps it to 270..1080; the TMX turns 900 at most),
//   except the vendor's own tokens for 270, 360 and 900. Let go, it goes back
//   to 0xFFFF, where t150_driver starts both wheels.

#include "../wheel_driver.h"

#include <cstdint>

namespace t150 {

constexpr std::uint8_t kConstantSlot = 0;
constexpr std::uint8_t kDamperSlot = 1;
constexpr int kLevelMax = 64;
constexpr int kDamperMax = 100;
constexpr int kDamperSafeMax = 80;

Bytes input_open();
Bytes input_closing();
Bytes input_close();
Bytes autocentre_force(std::uint16_t percent);
Bytes autocentre_kept(bool kept);
Bytes gain_full();
constexpr int kRangeLeast = 270;
constexpr int kRangeMost = 1080;
Bytes range(int degrees);
Bytes range_widest();

Bytes constant_first(std::uint8_t slot);
Bytes constant_update(std::uint8_t slot, int level);  // level -64 to 64, positive to the left
Bytes constant_commit(std::uint8_t slot);
Bytes damper_first(std::uint8_t slot);
Bytes damper_update(std::uint8_t slot, int coefficient);  // 0 to 80
Bytes damper_commit(std::uint8_t slot);
Bytes play(std::uint8_t slot);
Bytes stop(std::uint8_t slot);

}  // namespace t150

class T150Driver : public WheelDriver {
public:
    // `range_most`: the most it turns, 1080 on the T150, 900 on the TMX.
    explicit T150Driver(Transport& transport, int range_most = t150::kRangeMost)
        : transport_(transport), range_most_(range_most) {}

    bool open() override;
    bool set_force(double force) override;
    bool set_damper(double damper) override;
    bool set_friction(double friction) override;
    // The damper and friction go as one.
    bool play(double force, double damper, double friction) override;
    int set_rotation(int degrees) override;
    int rotation() const override { return rotation_; }
    void stop() override;
    void close() override;

private:
    bool send(const Bytes& packet) { return transport_.send(ReportType::output, 0, packet); }
    // The damper with friction added to it.
    bool send_resistance();

    Transport& transport_;
    int range_most_;
    int sent_force_ = 0;
    int sent_damper_ = 0;
    double damper_ = 0.0;
    double friction_ = 0.0;
    int rotation_ = 0;
    bool rotation_changed_ = false;
};
