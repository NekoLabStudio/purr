#pragma once

// Fanatec wheel bases in PC mode. Their protocol is close to Logitech's:
// seven-byte slot commands, byte 0 (slot << 4) | 1 to play or | 3 to stop,
// byte 1 the command (constant 0x08 in slot 0, the damper in slot 2 and
// friction in slot 4, both 0x0C), sent on the base's first output report.
// The game works the effects out itself, as the driver does. High
// resolution bases take the damper's and friction's coefficients in 8 bits
// (hid-ftecff.c's enable_highres_dynamic_effects: the 4-bit ones were too
// weak to feel on a DD1), the rest in 4. Rotation is ftec_set_range's three
// commands, from 90 degrees to the base's most; the base goes back to its
// most, as hid-fanatecff leaves it, once let go.
//
// Facts from gotzl/hid-fanatecff (hid-ftecff.c, hid-ftec.c, hid-ftec.h).
// Its level is new-lg4ff's TRANSLATE_FORCE: 0x80 (or 0x8000) is nothing and
// a positive Linux level goes above it, the same way a Logitech classic
// wheel turns to the left above 0x80 (lg4ff.h).

#include "../wheel_driver.h"

#include <cstdint>

namespace fanatec {

constexpr std::size_t kCommandLength = 7;

// The init sequence of ftecff_init, then ftec_set_range, which turns the
// base's own centring off (`range` in degrees).
std::vector<Bytes> start_commands(std::uint16_t range);
// Every slot stopped, as ftecff_init_slots leaves them.
std::vector<Bytes> slots_stopped();
// level -32767 to 32767; 0 stops the slot. High resolution bases
// (FTEC_HIGHRES) take 16 bits, the rest 8.
Bytes constant_force(int level, bool high_resolution);
// coefficient 0 to 32767; 0 stops the slot. `slot` is the damper's or
// friction's.
constexpr std::uint8_t kDamperSlot = 2;
constexpr std::uint8_t kFrictionSlot = 4;
constexpr int kRangeLeast = 90;
Bytes resistance(std::uint8_t slot, int coefficient, bool high_resolution);
Bytes damper(int coefficient, bool high_resolution);
// ftec_set_range: its centring off, then the range.
std::vector<Bytes> range_commands(std::uint16_t range);

}  // namespace fanatec

class FanatecDriver : public WheelDriver {
public:
    // `report_id` and `report_length` are the base's first output report's
    // (its length counting the ID byte).
    FanatecDriver(Transport& transport, std::uint8_t report_id, std::size_t report_length, bool high_resolution,
                  std::uint16_t range)
        : transport_(transport),
          report_id_(report_id),
          report_length_(report_length),
          high_resolution_(high_resolution),
          range_(range) {}

    bool open() override;
    bool set_force(double force) override;
    bool set_damper(double damper) override;
    bool set_friction(double friction) override;
    // hid-ftecff.c's timer runs every 2 ms.
    int max_update_hz() const override { return 500; }
    int set_rotation(int degrees) override;
    int rotation() const override { return rotation_; }
    void stop() override;
    void close() override;

private:
    bool send(const Bytes& command);
    bool send_range(int degrees);

    Transport& transport_;
    std::uint8_t report_id_;
    std::size_t report_length_;
    bool high_resolution_;
    std::uint16_t range_;
    Bytes sent_force_;
    Bytes sent_damper_;
    Bytes sent_friction_;
    int rotation_ = 0;
};
