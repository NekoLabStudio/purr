#include "t300.h"

#include "../../wheel_log.h"

#include <string>

namespace t300 {

namespace {

// Effect packets start 00, the effect's ID plus one, then the command.
constexpr std::uint8_t kPlayStop = 0x89;
constexpr std::uint8_t kConstant = 0x6A;
constexpr std::uint8_t kCondition = 0x64;
constexpr std::uint8_t kConditionUpdate = 0x4C;
constexpr std::uint8_t kPlayTimes = 0x41;
constexpr std::uint8_t kUpdate = 0x45;
constexpr std::uint8_t kDamperType = 0x07;
// hid-tmt300rs.c's t300rs_condition_effect_type gives 0x06 for a spring and
// 0x07 otherwise on upload; its condition update always sends 0x06.
constexpr std::uint8_t kConditionUpdateType = 0x06;
constexpr std::uint16_t kDamperSaturation = 0x7FFC;
// condition_values in hid-tmt300rs.c.
constexpr std::uint8_t kConditionFill[] = {0xFE, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF};

void put16(Bytes& out, int value) {
    const auto v = static_cast<std::uint16_t>(static_cast<std::int16_t>(value));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

Bytes header(std::uint8_t effect, std::uint8_t command) {
    return {0x00, static_cast<std::uint8_t>(effect + 1), command};
}

// t300rs_fill_timing: endless, no delay.
void timing(Bytes& out) {
    out.push_back(0x4F);
    put16(out, 0xFFFF);
    out.insert(out.end(), {0x00, 0x00});
    put16(out, 0);
    out.push_back(0x00);
    put16(out, 0xFFFF);
}

// Right and left coefficients, dead bands and saturations, as both condition packets start.
void condition(Bytes& out, int coefficient) {
    const int k = std::clamp(coefficient, 0, 32767);
    put16(out, k);
    put16(out, k);
    put16(out, 0);
    put16(out, 0);
    put16(out, kDamperSaturation);
    put16(out, kDamperSaturation);
}

}  // namespace

Bytes packet(std::size_t buffer, const Bytes& payload) {
    Bytes report{kReport};
    report.insert(report.end(), payload.begin(), payload.end());
    report.resize(buffer + 1, 0x00);
    return report;
}

Bytes gain_full(std::size_t buffer) {
    return packet(buffer, {0x02, 0xFF});
}

Bytes constant_upload(std::size_t buffer, std::uint8_t effect, int level) {
    Bytes out = header(effect, kConstant);
    put16(out, std::clamp(level, -16384, 16384));
    out.insert(out.end(), 8, 0x00);  // envelope
    out.push_back(0x00);
    timing(out);
    return packet(buffer, out);
}

Bytes constant_update(std::size_t buffer, std::uint8_t effect, int level) {
    Bytes out = header(effect, kConstant);
    put16(out, std::clamp(level, -16384, 16384));
    out.insert(out.end(), 8, 0x00);  // envelope
    out.push_back(0x00);
    out.push_back(kUpdate);
    put16(out, 0xFFFF);
    put16(out, 0);
    return packet(buffer, out);
}

Bytes damper_upload(std::size_t buffer, std::uint8_t effect, int coefficient) {
    Bytes out = header(effect, kCondition);
    condition(out, coefficient);
    out.insert(out.end(), std::begin(kConditionFill), std::end(kConditionFill));
    put16(out, kDamperSaturation);
    put16(out, kDamperSaturation);
    out.push_back(kDamperType);
    timing(out);
    return packet(buffer, out);
}

Bytes damper_update(std::size_t buffer, std::uint8_t effect, int coefficient) {
    Bytes out = header(effect, kConditionUpdate);
    condition(out, coefficient);
    out.push_back(kConditionUpdateType);
    out.push_back(kUpdate);
    put16(out, 0xFFFF);
    put16(out, 0);
    return packet(buffer, out);
}

Bytes range(std::size_t buffer, int degrees) {
    Bytes out = {0x08, 0x11};
    put16(out, degrees * 0x3C);
    return packet(buffer, out);
}

Bytes play(std::size_t buffer, std::uint8_t effect) {
    Bytes out = header(effect, kPlayStop);
    out.push_back(kPlayTimes);
    put16(out, 1);  // once, which for an endless effect is for good
    return packet(buffer, out);
}

Bytes stop(std::size_t buffer, std::uint8_t effect) {
    return packet(buffer, header(effect, kPlayStop));
}

}  // namespace t300

bool T300Driver::open() {
    sent_force_ = sent_damper_ = 0;
    rotation_ = 0;
    rotation_changed_ = false;
    if (max_output_report_ < buffer_ + 1) {
        wheel_log("its output report holds " + std::to_string(max_output_report_) + " bytes, its packets need " +
                  std::to_string(buffer_ + 1) + "; not opened");
        return false;
    }
    std::vector<Bytes> start;
    if (opening_ == t300::Opening::two_step) {
        start.push_back(t300::packet(buffer_, {0x01, 0x04}));
    }
    start.push_back(t300::packet(buffer_, {0x01, 0x05}));
    start.push_back(t300::gain_full(buffer_));
    // t300rs_set_autocenter(0): 08 04 01, then a strength of nothing.
    start.push_back(t300::packet(buffer_, {0x08, 0x04, 0x01, 0x00}));
    start.push_back(t300::packet(buffer_, {0x08, 0x03, 0x00, 0x00}));
    start.push_back(t300::constant_upload(buffer_, t300::kConstantEffect, 0));
    start.push_back(t300::play(buffer_, t300::kConstantEffect));
    start.push_back(t300::damper_upload(buffer_, t300::kDamperEffect, 0));
    start.push_back(t300::play(buffer_, t300::kDamperEffect));
    for (const Bytes& report : start) {
        if (!send(report)) {
            wheel_log("the wheel refused its start packets");
            return false;
        }
    }
    return true;
}

bool T300Driver::set_force(double force) {
    // Thrustmaster's driver uses half the 16-bit range (t300rs_calculate_constant_level).
    const int level = static_cast<int>(std::lround(steps(force, 32767) / 2.0));
    if (level == sent_force_) {
        return true;
    }
    sent_force_ = level;
    return send(t300::constant_update(buffer_, t300::kConstantEffect, level));
}

bool T300Driver::set_damper(double damper) {
    const int coefficient = steps(damper, 32767, 0.0);
    if (coefficient == sent_damper_) {
        return true;
    }
    sent_damper_ = coefficient;
    return send(t300::damper_update(buffer_, t300::kDamperEffect, coefficient));
}

int T300Driver::set_rotation(int degrees) {
    degrees = std::clamp(degrees, range_least_, range_most_);
    if (degrees == rotation_) {
        return rotation_;
    }
    if (!send(t300::range(buffer_, degrees))) {
        wheel_log("the wheel refused its rotation");
        return 0;
    }
    rotation_ = degrees;
    rotation_changed_ = true;
    return rotation_;
}

void T300Driver::stop() {
    set_force(0.0);
    set_damper(0.0);
}

void T300Driver::close() {
    stop();
    if (rotation_changed_) {
        send(t300::range(buffer_, range_most_));
        rotation_changed_ = false;
        rotation_ = 0;
    }
    send(t300::stop(buffer_, t300::kConstantEffect));
    send(t300::stop(buffer_, t300::kDamperEffect));
    if (opening_ == t300::Opening::two_step) {
        send_payload({0x01, 0x05});
    }
    send_payload({0x01, 0x00});
}
