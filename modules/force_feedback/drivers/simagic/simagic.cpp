#include "simagic.h"

#include "../../wheel_log.h"

namespace simagic {

namespace {

constexpr std::uint8_t kSetEffect = 0x01;
constexpr std::uint8_t kSetCondition = 0x03;
constexpr std::uint8_t kSetConstant = 0x05;
constexpr std::uint8_t kOperation = 0x0A;
constexpr std::uint8_t kGain = 0x40;
constexpr std::uint8_t kStart = 0x01;
constexpr std::uint8_t kStop = 0x03;
constexpr std::uint16_t kEndless = 0xFFFF;
// The longest packet, a condition's.
constexpr std::size_t kLongest = 15;

void put16(Bytes& out, int value) {
    const auto v = static_cast<std::uint16_t>(static_cast<std::int16_t>(value));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

}  // namespace

Bytes set_effect(std::uint8_t block) {
    Bytes out = {kSetEffect, block, 0x01};
    put16(out, kEndless);
    out.insert(out.end(), {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x04, 0x3F});
    return out;
}

Bytes set_constant(int magnitude) {
    Bytes out = {kSetConstant, kConstantBlock};
    put16(out, std::clamp(magnitude, -kFull, kFull));
    return out;
}

Bytes set_condition(std::uint8_t block, int coefficient) {
    const int k = std::clamp(coefficient, 0, kFull);
    Bytes out = {kSetCondition, block, 0x00};
    put16(out, 0);      // centre
    put16(out, k);      // right coefficient
    put16(out, k);      // left coefficient
    put16(out, kFull);  // right saturation
    put16(out, kFull);  // left saturation
    put16(out, 0);      // dead band
    return out;
}

Bytes start(std::uint8_t block) {
    return {kOperation, block, kStart, 0x01, 0x00};
}

Bytes stop(std::uint8_t block) {
    return {kOperation, block, kStop, 0x00, 0x00};
}

Bytes gain_full() {
    return {kGain, 0xFF};
}

}  // namespace simagic

bool SimagicDriver::send(const Bytes& packet) {
    Bytes report;
    if (report_id_ != 0) {
        report.push_back(report_id_);
    }
    report.insert(report.end(), packet.begin(), packet.end());
    report.resize(std::max(report_length_, report.size()), 0x00);
    return transport_.send(ReportType::output, report_id_, report);
}

bool SimagicDriver::open() {
    sent_force_ = sent_damper_ = sent_friction_ = 0;
    if (report_length_ < simagic::kLongest + (report_id_ != 0 ? 1 : 0)) {
        wheel_log("its output report is too short for Simagic's packets; not opened");
        return false;
    }
    // As simagic-ff uploads each effect (Set Effect, then its values) and
    // plays it, once each.
    const Bytes start[] = {
        simagic::gain_full(),
        simagic::set_effect(simagic::kConstantBlock),
        simagic::set_constant(0),
        simagic::start(simagic::kConstantBlock),
        simagic::set_effect(simagic::kDamperBlock),
        simagic::set_condition(simagic::kDamperBlock, 0),
        simagic::start(simagic::kDamperBlock),
        simagic::set_effect(simagic::kFrictionBlock),
        simagic::set_condition(simagic::kFrictionBlock, 0),
        simagic::start(simagic::kFrictionBlock),
    };
    for (const Bytes& packet : start) {
        if (!send(packet)) {
            wheel_log("the base refused its start packets");
            return false;
        }
    }
    return true;
}

bool SimagicDriver::set_force(double force) {
    const int magnitude = steps(force, simagic::kFull);
    if (magnitude == sent_force_) {
        return true;
    }
    sent_force_ = magnitude;
    return send(simagic::set_constant(magnitude));
}

bool SimagicDriver::set_damper(double damper) {
    const int coefficient = steps(damper, simagic::kFull, 0.0);
    if (coefficient == sent_damper_) {
        return true;
    }
    sent_damper_ = coefficient;
    return send(simagic::set_condition(simagic::kDamperBlock, coefficient));
}

bool SimagicDriver::set_friction(double friction) {
    const int coefficient = steps(friction, simagic::kFull, 0.0);
    if (coefficient == sent_friction_) {
        return true;
    }
    sent_friction_ = coefficient;
    return send(simagic::set_condition(simagic::kFrictionBlock, coefficient));
}

void SimagicDriver::stop() {
    set_force(0.0);
    set_damper(0.0);
    set_friction(0.0);
}

void SimagicDriver::close() {
    stop();
    for (const std::uint8_t block : {simagic::kConstantBlock, simagic::kDamperBlock, simagic::kFrictionBlock}) {
        send(simagic::stop(block));
    }
}
