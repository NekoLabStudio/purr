#pragma once

// Thrustmaster's T300 family: the T300RS (and GT) in PS3 normal and
// advanced modes and in PS4 mode, the TX, T248, TS-XW and TS-PC. Every
// packet is output report 0x60, padded to the wheel's buffer: 63 bytes, or
// 31 in the T300RS's PS4 mode.
//
// Facts from Kimplul/hid-tmff2 (src/tmt300rs and each model's file) and
// Akellacom/thrustmaster_t300rs_gt_macos_driver (tested on one T300RS).
// The wheel only keeps up with so many reports a second (hid-tmff2's
// DEFAULT_TIMER_PERIOD, 8 ms, with the comment that sending faster fills
// its buffer), so the thread plays to it at that pace. Friction is left
// out: hid-tmt300rs.c uploads it as the very packet it uploads a damper as.
// Rotation is t300rs_set_range's 08 11 with the degrees times 0x3C, within
// each model's range (40 to 1080 on the T300RS, from 140 on the rest, up
// to 900 on the T248 and TX); the wheel goes back to its most once let go.
// Unproven: whether the firmware takes these through IOHIDDeviceSetReport.
// Akellacom's driver says it ignores SET_REPORT and seizes the interface,
// which the game won't do; crossover-wheel measured the opposite on the
// T150. A wheel whose output report is too short for the packets isn't opened.

#include "../wheel_driver.h"

#include <cstdint>

namespace t300 {

constexpr std::uint8_t kReport = 0x60;
constexpr std::size_t kBuffer = 63;
constexpr std::size_t kPs4Buffer = 31;
constexpr std::uint8_t kConstantEffect = 0;
constexpr std::uint8_t kDamperEffect = 1;
constexpr int kUpdateHz = 125;

// How a model opens and closes: the T300RS sends 01 05 and 01 00; the
// T248, TX, TS-XW and TS-PC send 01 04 then 01 05, and 01 05 then 01 00.
enum class Opening { t300rs, two_step };

// Each is a whole report: 0x60, then the packet padded to `buffer` bytes.
Bytes packet(std::size_t buffer, const Bytes& payload);
Bytes gain_full(std::size_t buffer);
Bytes constant_upload(std::size_t buffer, std::uint8_t effect, int level);  // level -16384 to 16384
Bytes constant_update(std::size_t buffer, std::uint8_t effect, int level);
Bytes damper_upload(std::size_t buffer, std::uint8_t effect, int coefficient);  // 0 to 32767
Bytes damper_update(std::size_t buffer, std::uint8_t effect, int coefficient);
Bytes range(std::size_t buffer, int degrees);
Bytes play(std::size_t buffer, std::uint8_t effect);
Bytes stop(std::size_t buffer, std::uint8_t effect);

}  // namespace t300

class T300Driver : public WheelDriver {
public:
    // `range_least` and `range_most`: the model's rotation, in degrees.
    T300Driver(Transport& transport, std::size_t buffer, t300::Opening opening, std::size_t max_output_report,
               int range_least = 40, int range_most = 1080)
        : transport_(transport),
          buffer_(buffer),
          opening_(opening),
          max_output_report_(max_output_report),
          range_least_(range_least),
          range_most_(range_most) {}

    bool open() override;
    bool set_force(double force) override;
    bool set_damper(double damper) override;
    int max_update_hz() const override { return t300::kUpdateHz; }
    int set_rotation(int degrees) override;
    int rotation() const override { return rotation_; }
    void stop() override;
    void close() override;

private:
    bool send(const Bytes& report) { return transport_.send(ReportType::output, t300::kReport, report); }
    bool send_payload(const Bytes& payload) { return send(t300::packet(buffer_, payload)); }

    Transport& transport_;
    std::size_t buffer_;
    t300::Opening opening_;
    std::size_t max_output_report_;
    int range_least_;
    int range_most_;
    int sent_force_ = 0;
    int sent_damper_ = 0;
    int rotation_ = 0;
    bool rotation_changed_ = false;
};
