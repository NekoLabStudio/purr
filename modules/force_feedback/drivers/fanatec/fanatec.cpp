#include "fanatec.h"

#include "../../wheel_log.h"

namespace fanatec {

namespace {

constexpr std::uint8_t kPlay = 0x01;
constexpr std::uint8_t kStop = 0x03;
constexpr std::uint8_t kConstantSlot = 0x00;
constexpr std::uint8_t kConstant = 0x08;
constexpr std::uint8_t kSpring = 0x0B;
constexpr std::uint8_t kResistance = 0x0C;  // damper, inertia and friction share it

Bytes command(std::initializer_list<std::uint8_t> bytes) {
    Bytes out(bytes);
    out.resize(kCommandLength, 0x00);
    return out;
}

}  // namespace

std::vector<Bytes> range_commands(std::uint16_t range) {
    return {
        command({0xF5}),
        command({0xF8, 0x09, 0x01, 0x06, 0x01}),
        command({0xF8, 0x81, static_cast<std::uint8_t>(range & 0xFF), static_cast<std::uint8_t>(range >> 8)}),
    };
}

std::vector<Bytes> start_commands(std::uint16_t range) {
    std::vector<Bytes> out = {
        command({0xF8, 0x09, 0x01, 0x06, 0xFF, 0x01}),
        command({0xF8, 0x09, 0x01, 0x06, 0xFF, 0x00}),
        command({0xF8, 0x09, 0x01, 0x06, 0xFF, 0x04}),
    };
    const std::vector<Bytes> ranged = range_commands(range);
    out.insert(out.end(), ranged.begin(), ranged.end());
    return out;
}

std::vector<Bytes> slots_stopped() {
    // ftecff_update_slot with nothing to play: each slot stopped, and the
    // resistance slots' values reset (clip 0xFF).
    return {
        command({0x00 | kStop, kConstant}),
        command({0x10 | kStop, kSpring}),
        command({0x20 | kStop, kResistance, 0x00, 0x00, 0x00, 0x00, 0xFF}),
        command({0x30 | kStop, kResistance, 0x00, 0x00, 0x00, 0x00, 0xFF}),
        command({0x40 | kStop, kResistance, 0x00, 0x00, 0x00, 0x00, 0xFF}),
    };
}

Bytes constant_force(int level, bool high_resolution) {
    if (level == 0) {
        return command({kConstantSlot | kStop, kConstant});
    }
    const int translated = std::clamp(level, -0x8000, 0x7FFF) + 0x8000;  // TRANSLATE_FORCE
    if (high_resolution) {
        return command({kConstantSlot | kPlay, kConstant, static_cast<std::uint8_t>(translated & 0xFF),
                        static_cast<std::uint8_t>(translated >> 8), 0x00, 0x00, 0x01});
    }
    return command({kConstantSlot | kPlay, kConstant, static_cast<std::uint8_t>(translated >> 8)});
}

Bytes resistance(std::uint8_t slot, int coefficient, bool high_resolution) {
    const auto at = static_cast<std::uint8_t>(slot << 4);
    if (coefficient <= 0) {
        return command({static_cast<std::uint8_t>(at | kStop), kResistance, 0x00, 0x00, 0x00, 0x00, 0xFF});
    }
    // SCALE_COEFF(k, 8 or 4): the coefficient's top bits, both ways, the
    // bytes between 0 (non-zero, they oscillated on a DD1); clip full.
    const auto k = static_cast<std::uint8_t>(std::min(coefficient * 2, 0xFFFF) >> (high_resolution ? 8 : 12));
    return command({static_cast<std::uint8_t>(at | kPlay), kResistance, k, 0x00, k, 0x00, 0xFF});
}

Bytes damper(int coefficient, bool high_resolution) {
    return resistance(kDamperSlot, coefficient, high_resolution);
}

}  // namespace fanatec

bool FanatecDriver::send(const Bytes& command) {
    Bytes report;
    if (report_id_ != 0) {
        report.push_back(report_id_);
    }
    report.insert(report.end(), command.begin(), command.end());
    report.resize(std::max(report_length_, report.size()), 0x00);
    return transport_.send(ReportType::output, report_id_, report);
}

bool FanatecDriver::open() {
    sent_force_ = fanatec::constant_force(0, high_resolution_);
    sent_damper_ = fanatec::resistance(fanatec::kDamperSlot, 0, high_resolution_);
    sent_friction_ = fanatec::resistance(fanatec::kFrictionSlot, 0, high_resolution_);
    rotation_ = range_;
    if (report_length_ < fanatec::kCommandLength + (report_id_ != 0 ? 1 : 0)) {
        wheel_log("its output report is too short for Fanatec's commands; not opened");
        return false;
    }
    std::vector<Bytes> start = fanatec::start_commands(range_);
    const std::vector<Bytes> slots = fanatec::slots_stopped();
    start.insert(start.end(), slots.begin(), slots.end());
    for (const Bytes& command : start) {
        if (!send(command)) {
            wheel_log("the base refused its start commands");
            return false;
        }
    }
    return true;
}

// Compared as commands: an 8-bit base or the 4-bit damper has fewer steps than the level.
bool FanatecDriver::set_force(double force) {
    Bytes command = fanatec::constant_force(steps(force, 32767), high_resolution_);
    if (command == sent_force_) {
        return true;
    }
    sent_force_ = command;
    return send(command);
}

bool FanatecDriver::set_damper(double damper) {
    Bytes command = fanatec::resistance(fanatec::kDamperSlot, steps(damper, 32767, 0.0), high_resolution_);
    if (command == sent_damper_) {
        return true;
    }
    sent_damper_ = command;
    return send(command);
}

bool FanatecDriver::set_friction(double friction) {
    Bytes command = fanatec::resistance(fanatec::kFrictionSlot, steps(friction, 32767, 0.0), high_resolution_);
    if (command == sent_friction_) {
        return true;
    }
    sent_friction_ = command;
    return send(command);
}

bool FanatecDriver::send_range(int degrees) {
    for (const Bytes& command : fanatec::range_commands(static_cast<std::uint16_t>(degrees))) {
        if (!send(command)) {
            return false;
        }
    }
    return true;
}

int FanatecDriver::set_rotation(int degrees) {
    degrees = std::clamp(degrees, fanatec::kRangeLeast, static_cast<int>(range_));
    if (degrees == rotation_) {
        return rotation_;
    }
    if (!send_range(degrees)) {
        wheel_log("the base refused its rotation");
        return 0;
    }
    rotation_ = degrees;
    return rotation_;
}

void FanatecDriver::stop() {
    set_force(0.0);
    set_damper(0.0);
    set_friction(0.0);
}

void FanatecDriver::close() {
    stop();
    if (rotation_ != range_) {
        send_range(range_);
        rotation_ = range_;
    }
}
