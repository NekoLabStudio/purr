#include "t150.h"

#include "../../wheel_log.h"

namespace t150 {

namespace {

constexpr std::uint8_t kInput = 0x42;
constexpr std::uint8_t kSettings = 0x40;
constexpr std::uint8_t kAutocentreForce = 0x03;
constexpr std::uint8_t kAutocentreKept = 0x04;
constexpr std::uint8_t kRange = 0x11;
constexpr std::uint8_t kGain = 0x43;
constexpr std::uint8_t kGainFull = 0x80;

// ff_first's class, ff_update's class (a different set), ff_commit's lead
// and its effect types.
constexpr std::uint8_t kFirstConstant = 0x02;
constexpr std::uint8_t kFirstCondition = 0x05;
constexpr std::uint8_t kUpdateConstant = 0x03;
constexpr std::uint8_t kUpdateCondition = 0x05;
constexpr std::uint8_t kCommit = 0x01;
constexpr std::uint16_t kTypeConstant = 0x4000;
constexpr std::uint16_t kTypeDamper = 0x4041;
constexpr std::uint16_t kEndless = 0xFFFF;
// A damper's ff_first ends with its saturations, as Thrustmaster's driver sends them.
constexpr std::uint8_t kDamperSaturation = 0x64;

constexpr std::uint8_t kControl = 0x41;
constexpr std::uint8_t kControlPlay = 0x41;
constexpr std::uint8_t kControlStop = 0x00;

// The two keys tying an effect's packets together, 16 bits little-endian.
std::uint16_t first_key(std::uint8_t slot) {
    return static_cast<std::uint16_t>(slot * 0x1C + 0x1C);
}

std::uint16_t update_key(std::uint8_t slot) {
    return static_cast<std::uint16_t>(slot * 0x1C + 0x0E);
}

std::uint8_t lo(std::uint16_t value) {
    return static_cast<std::uint8_t>(value & 0xFF);
}

std::uint8_t hi(std::uint16_t value) {
    return static_cast<std::uint8_t>(value >> 8);
}

// ff_commit: lead, slot, type, duration, two zero fields, the update key,
// the first key, the start delay.
Bytes commit(std::uint8_t slot, std::uint16_t type) {
    return {kCommit, slot, lo(type), hi(type), lo(kEndless), hi(kEndless), 0x00, 0x00, 0x00,
            lo(update_key(slot)), hi(update_key(slot)), lo(first_key(slot)), hi(first_key(slot)), 0x00, 0x00};
}

}  // namespace

Bytes input_open() {
    return {kInput, 0x04};
}

Bytes input_closing() {
    return {kInput, 0x05};
}

Bytes input_close() {
    return {kInput, 0x00};
}

Bytes autocentre_force(std::uint16_t percent) {
    return {kSettings, kAutocentreForce, lo(percent), hi(percent)};
}

Bytes autocentre_kept(bool kept) {
    return {kSettings, kAutocentreKept, static_cast<std::uint8_t>(kept ? 1 : 0), 0x00};
}

Bytes gain_full() {
    return {kGain, kGainFull};
}

Bytes range(int degrees) {
    // The vendor's driver sends its own tokens for the ranges it names; t150_driver scales the rest.
    std::uint16_t value;
    switch (degrees) {
    case 270:
        value = 0x3FFF;
        break;
    case 360:
        value = 0x5555;
        break;
    case 900:
        value = 0xD555;
        break;
    default:
        value = static_cast<std::uint16_t>(degrees * 0xFFFF / 1080);
        break;
    }
    return {kSettings, kRange, lo(value), hi(value)};
}

Bytes range_widest() {
    return {kSettings, kRange, 0xFF, 0xFF};
}

Bytes constant_first(std::uint8_t slot) {
    // Class, key, then an envelope of nothing: nine bytes, no trailer.
    return {kFirstConstant, lo(first_key(slot)), hi(first_key(slot)), 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
}

Bytes constant_update(std::uint8_t slot, int level) {
    const auto wire = static_cast<std::uint8_t>(static_cast<std::int8_t>(std::clamp(level, -kLevelMax, kLevelMax)));
    return {kUpdateConstant, lo(update_key(slot)), hi(update_key(slot)), wire};
}

Bytes constant_commit(std::uint8_t slot) {
    return commit(slot, kTypeConstant);
}

Bytes damper_first(std::uint8_t slot) {
    return {kFirstCondition, lo(first_key(slot)), hi(first_key(slot)), 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            kDamperSaturation, kDamperSaturation};
}

Bytes damper_update(std::uint8_t slot, int coefficient) {
    const auto k = static_cast<std::uint8_t>(std::clamp(coefficient, 0, kDamperSafeMax));
    // Class, key, right and left coefficients, centre, dead band, right and left saturations.
    return {kUpdateCondition, lo(update_key(slot)), hi(update_key(slot)), k, k, 0x00, 0x00, 0x00, 0x00,
            kDamperSaturation, kDamperSaturation};
}

Bytes damper_commit(std::uint8_t slot) {
    return commit(slot, kTypeDamper);
}

Bytes play(std::uint8_t slot) {
    return {kControl, slot, kControlPlay, 0x01};
}

Bytes stop(std::uint8_t slot) {
    return {kControl, slot, kControlStop, 0x01};
}

}  // namespace t150

bool T150Driver::open() {
    sent_force_ = sent_damper_ = 0;
    damper_ = friction_ = 0.0;
    rotation_ = 0;
    rotation_changed_ = false;
    const Bytes start[] = {
        t150::input_open(),
        t150::autocentre_force(0),
        t150::autocentre_kept(false),
        t150::gain_full(),
        t150::constant_first(t150::kConstantSlot),
        t150::constant_update(t150::kConstantSlot, 0),
        t150::constant_commit(t150::kConstantSlot),
        t150::play(t150::kConstantSlot),
        t150::damper_first(t150::kDamperSlot),
        t150::damper_update(t150::kDamperSlot, 0),
        t150::damper_commit(t150::kDamperSlot),
        t150::play(t150::kDamperSlot),
    };
    for (const Bytes& packet : start) {
        if (!send(packet)) {
            wheel_log("the wheel refused its start packets");
            return false;
        }
    }
    return true;
}

bool T150Driver::set_force(double force) {
    const int level = steps(force, t150::kLevelMax);
    if (level == sent_force_) {
        return true;
    }
    sent_force_ = level;
    return send(t150::constant_update(t150::kConstantSlot, level));
}

bool T150Driver::send_resistance() {
    const int coefficient = std::min(steps(damper_ + friction_, t150::kDamperMax, 0.0), t150::kDamperSafeMax);
    if (coefficient == sent_damper_) {
        return true;
    }
    sent_damper_ = coefficient;
    return send(t150::damper_update(t150::kDamperSlot, coefficient));
}

bool T150Driver::set_damper(double damper) {
    damper_ = std::clamp(damper, 0.0, 1.0);
    return send_resistance();
}

bool T150Driver::set_friction(double friction) {
    friction_ = std::clamp(friction, 0.0, 1.0);
    return send_resistance();
}

bool T150Driver::play(double force, double damper, double friction) {
    damper_ = std::clamp(damper, 0.0, 1.0);
    friction_ = std::clamp(friction, 0.0, 1.0);
    return set_force(force) && send_resistance();
}

int T150Driver::set_rotation(int degrees) {
    degrees = std::clamp(degrees, t150::kRangeLeast, range_most_);
    if (degrees == rotation_) {
        return rotation_;
    }
    if (!send(t150::range(degrees))) {
        wheel_log("the wheel refused its rotation");
        return 0;
    }
    rotation_ = degrees;
    rotation_changed_ = true;
    return rotation_;
}

void T150Driver::stop() {
    set_force(0.0);
    set_damper(0.0);
    set_friction(0.0);
}

void T150Driver::close() {
    stop();
    if (rotation_changed_) {
        send(t150::range_widest());
        rotation_changed_ = false;
        rotation_ = 0;
    }
    send(t150::stop(t150::kConstantSlot));
    send(t150::stop(t150::kDamperSlot));
    send(t150::input_closing());
    send(t150::input_closing());
    send(t150::input_close());
}
