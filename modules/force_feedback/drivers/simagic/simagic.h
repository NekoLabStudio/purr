#pragma once

// Simagic bases on firmware after v159 (around August 2023: the Alpha,
// Alpha Mini, Alpha U and EVO), which dropped the HID PID descriptor for
// reports of their own. Older firmware still speaks PID and goes to that
// driver (the registry tells them apart by the descriptor).
//
// Facts from JacKeTUs/simagic-ff (hid-simagic.c), written here from them:
// - Every packet goes in the base's first output report, padded to its
//   length: its first byte says what it is.
// - 0x01 Set Effect: the effect's block, 1, the duration (0xFFFF endless,
//   little-endian), four zeros, the gain (0xFF), the trigger button (0xFF),
//   then 0x04 0x3F.
// - 0x05 Set Constant: the block, the magnitude from -10000 to 10000.
// - 0x03 Set Condition: the block, 0, then the centre, right and left
//   coefficients (-10000 to 10000), right and left saturations (0 to 10000)
//   and the dead band, each little-endian.
// - 0x0A Effect Operation: the block, 1 to start (with a loop count) or 3
//   to stop, then 0. 0x40 sets the gain (its top byte).
// - Each kind of effect has a fixed block: the constant force 1, the damper
//   5, friction 7.
// The driver takes the magnitude straight from a Linux level, so a positive
// one turns the wheel to the left as with the other PID wheels; a tester
// should confirm it. Rotation is SimPro Manager's: simagic-ff warns that a
// range set from outside it can drop force feedback until a restart.

#include "../wheel_driver.h"

#include <cstdint>

namespace simagic {

constexpr std::uint8_t kConstantBlock = 0x01;
constexpr std::uint8_t kDamperBlock = 0x05;
constexpr std::uint8_t kFrictionBlock = 0x07;
constexpr int kFull = 10000;

// Each is the packet's own bytes, before the report's padding.
Bytes set_effect(std::uint8_t block);
Bytes set_constant(int magnitude);                         // -10000 to 10000
Bytes set_condition(std::uint8_t block, int coefficient);  // 0 to 10000, both ways
Bytes start(std::uint8_t block);
Bytes stop(std::uint8_t block);
Bytes gain_full();

}  // namespace simagic

class SimagicDriver : public WheelDriver {
public:
    // `report_id` and `report_length` are the base's first output report's
    // (its length counting the ID byte).
    SimagicDriver(Transport& transport, std::uint8_t report_id, std::size_t report_length)
        : transport_(transport), report_id_(report_id), report_length_(report_length) {}

    bool open() override;
    bool set_force(double force) override;
    bool set_damper(double damper) override;
    bool set_friction(double friction) override;
    // No reference gives a rate; simagic-ff's queue fills when it's flooded.
    int max_update_hz() const override { return 500; }
    void stop() override;
    void close() override;

private:
    bool send(const Bytes& packet);

    Transport& transport_;
    std::uint8_t report_id_;
    std::size_t report_length_;
    int sent_force_ = 0;
    int sent_damper_ = 0;
    int sent_friction_ = 0;
};
