#include "lg4ff.h"

#include "../../wheel_log.h"

#include <cstdlib>

namespace lg4ff {

namespace {

// The effect types in byte 1.
constexpr std::uint8_t kConstant = 0x00;
constexpr std::uint8_t kDamper = 0x0C;
constexpr std::uint8_t kFriction = 0x0E;
// A condition's clip: its full strength.
constexpr std::uint8_t kClipFull = 0xFF;

constexpr std::uint16_t kDrivingForceEx = 0xC294;
constexpr std::uint16_t kMomo = 0xC295;
constexpr std::uint16_t kMomoRacing = 0xCA03;
constexpr std::uint16_t kDrivingForcePro = 0xC298;
constexpr std::uint16_t kG25 = 0xC299;
constexpr std::uint16_t kDrivingForceGt = 0xC29A;
constexpr std::uint16_t kG27 = 0xC29B;
constexpr std::uint16_t kG29 = 0xC24F;
constexpr std::uint16_t kG923 = 0xC266;
constexpr std::uint16_t kG923PlayStationMode = 0xC267;

Bytes command(std::initializer_list<std::uint8_t> bytes) {
    Bytes out(bytes);
    out.resize(kCommandLength, 0x00);
    return out;
}

std::uint8_t slot_byte(int slot, std::uint8_t op) {
    return static_cast<std::uint8_t>((0x10 << slot) + op);
}

// SCALE_COEFF: the coefficient doubled into 16 bits, its top `bits` kept.
std::uint8_t scale_coefficient(int coefficient, int bits) {
    return static_cast<std::uint8_t>(std::min(std::abs(coefficient) * 2, 0xFFFF) >> (16 - bits));
}

// hid-lg4ff.c's lg4ff_main_checklist, in its order: which wheel a release
// number says this is, when it reports itself as a Driving Force (DF-EX).
struct Identity {
    std::uint16_t mask;
    std::uint16_t result;
    std::uint16_t product;
};
constexpr Identity kIdentities[] = {
    {0xFFF8, 0x1350, kG29},  {0xFF00, 0x8900, kG29}, {0xFF00, 0x1300, kDrivingForceGt},
    {0xFFF0, 0x1230, kG27},  {0xFF00, 0x1200, kG25}, {0xF000, 0x1000, kDrivingForcePro},
};

// Each wheel's "switch to native mode" commands, from DF-EX mode
// (lg4ff_get_mode_switch_command with the target its own product).
std::vector<Bytes> switch_commands(std::uint16_t product) {
    const Bytes revert_on_reset = command({0xF8, 0x0A});  // EXT_CMD10
    switch (product) {
    case kDrivingForcePro:
        return {command({0xF8, 0x01})};  // EXT_CMD1
    case kG25:
        return {command({0xF8, 0x10})};  // EXT_CMD16
    case kDrivingForceGt:
        return {revert_on_reset, command({0xF8, 0x09, 0x03, 0x01})};  // EXT_CMD9
    case kG27:
        return {revert_on_reset, command({0xF8, 0x09, 0x04, 0x01})};
    case kG29:
        return {revert_on_reset, command({0xF8, 0x09, 0x05, 0x01, 0x01})};
    default:
        return {};
    }
}

}  // namespace

Bytes constant_force(std::uint8_t op, int level) {
    if (op == kStop) {
        return command({slot_byte(kConstantSlot, kStop)});
    }
    // 0x80 is no force; above it turns the wheel to the left (measured on a G923).
    return command({slot_byte(kConstantSlot, op), kConstant,
                    static_cast<std::uint8_t>(std::clamp(0x80 + level, 1, 0xFF))});
}

Bytes damper(int slot, std::uint8_t op, int coefficient) {
    if (op == kStop) {
        return command({slot_byte(slot, kStop)});
    }
    const std::uint8_t k = scale_coefficient(coefficient, 4);
    return command({slot_byte(slot, op), kDamper, k, 0x00, k, 0x00, kClipFull});
}

Bytes friction(std::uint8_t op, int coefficient) {
    if (op == kStop) {
        return command({slot_byte(kFrictionSlot, kStop)});
    }
    const std::uint8_t k = scale_coefficient(coefficient, 8);
    return command({slot_byte(kFrictionSlot, op), kFriction, k, k, kClipFull, 0x00, 0x00});
}

Bytes stop_all() {
    return command({0xF3});
}

Bytes autocentre_off() {
    return command({0xF5});
}

Bytes autocentre_off_formula_force_ex() {
    return command({0xFE, 0x03});
}

Bytes range(int degrees) {
    return command({0xF8, 0x81, static_cast<std::uint8_t>(degrees & 0xFF), static_cast<std::uint8_t>(degrees >> 8)});
}

std::vector<Bytes> range_driving_force_pro(int degrees) {
    // A coarse limit of 200 or 900, then a fine one inside it (none at either).
    const int full = degrees > 200 ? 900 : 200;
    std::vector<Bytes> out = {command({0xF8, static_cast<std::uint8_t>(degrees > 200 ? 0x03 : 0x02)})};
    if (degrees == 200 || degrees == 900) {
        out.push_back(command({0x81, 0x0B}));
        return out;
    }
    const int left = (full - degrees + 1) * 2047 / full;
    const int right = 0xFFF - left;
    out.push_back(command({0x81, 0x0B, static_cast<std::uint8_t>(left >> 4), static_cast<std::uint8_t>(right >> 4), 0xFF,
                           static_cast<std::uint8_t>(((right & 0x0E) << 4) | (left & 0x0E)), 0xFF}));
    return out;
}

Bytes leds(std::uint8_t mask) {
    return command({0xF8, 0x12, static_cast<std::uint8_t>(mask & kLedMask)});
}

Model model(std::uint16_t product) {
    switch (product) {
    case kDrivingForcePro:
        return {true, Model::Range::driving_force_pro};
    case kG25:
    case kDrivingForceGt:
        return {true, Model::Range::g25};
    case kG27:
        return {true, Model::Range::g25, true};
    case kG29:
    case kG923:
    case kG923PlayStationMode:
        return {false, Model::Range::g25, true};
    case kDrivingForceEx:
    case kMomo:
    case kMomoRacing:
    default:
        return {false, Model::Range::none};
    }
}

bool is_formula_force_ex(std::uint16_t product, std::uint16_t version) {
    // LG4FF_FFEX_REV_MAJ and _MIN.
    return product == kDrivingForceEx && version == 0x2100;
}

std::optional<ModeSwitch> native_mode_switch(std::uint16_t product, std::uint16_t version) {
    if (product == kDrivingForceEx) {
        for (const Identity& identity : kIdentities) {
            if ((version & identity.mask) == identity.result) {
                return ModeSwitch{0, switch_commands(identity.product), identity.product};
            }
        }
        return std::nullopt;  // a Driving Force or a Formula Force EX: it has no other mode
    }
    // new-lg4ff's lg4ff_g923_ident_info and lg4ff_mode_switch_30_g923: the
    // G923 in PlayStation mode takes its switch on report 0x30.
    if (product == kG923PlayStationMode && (version & 0xFF00) == 0x3800) {
        return ModeSwitch{0x30, {Bytes{0x30, 0xF8, 0x09, 0x07, 0x01, 0x01, 0x00, 0x00}}, kG923};
    }
    return std::nullopt;
}

}  // namespace lg4ff

bool Lg4ffDriver::open() {
    // As new-lg4ff starts a wheel: every slot stopped, its own centring
    // off, then the constant force downloaded at nothing (lg4ff_init_slots).
    sent_force_ = 0;
    sent_damper_ = lg4ff::damper(lg4ff::kDamperSlot, lg4ff::kStop, 0);
    sent_friction_ = lg4ff::damper(lg4ff::kFrictionSlot, lg4ff::kStop, 0);
    rotation_ = 0;
    rotation_changed_ = false;
    sent_leds_.reset();
    const bool stopped = send(lg4ff::stop_all());
    const bool free = send(formula_force_ex_ ? lg4ff::autocentre_off_formula_force_ex() : lg4ff::autocentre_off());
    constant_playing_ = send(lg4ff::constant_force(lg4ff::kDownload, 0));
    if (!stopped || !free || !constant_playing_) {
        wheel_log("the wheel refused its start commands");
        return false;
    }
    return true;
}

bool Lg4ffDriver::set_force(double force) {
    const int level = steps(force, 127);
    if (constant_playing_ && level == sent_force_) {
        return true;
    }
    const std::uint8_t op = constant_playing_ ? lg4ff::kRefresh : lg4ff::kDownload;
    sent_force_ = level;
    constant_playing_ = true;
    return send(lg4ff::constant_force(op, level));
}

bool Lg4ffDriver::update(Bytes& sent, int slot, double share, bool as_friction) {
    share = std::clamp(share, 0.0, 1.0);
    const bool playing = (sent[0] & 0x0F) != lg4ff::kStop;
    const std::uint8_t op = share <= 0.0 ? lg4ff::kStop : playing ? lg4ff::kRefresh : lg4ff::kDownload;
    const int coefficient = steps(share, 32767, 0.0);
    const Bytes command = as_friction ? lg4ff::friction(op, coefficient) : lg4ff::damper(slot, op, coefficient);
    Bytes as_sent = command;
    if ((as_sent[0] & 0x0F) == lg4ff::kDownload) {
        as_sent[0] = static_cast<std::uint8_t>((as_sent[0] & 0xF0) | lg4ff::kRefresh);
    }
    if (as_sent == sent) {
        return true;
    }
    sent = as_sent;
    return send(command);
}

bool Lg4ffDriver::set_damper(double damper) {
    return update(sent_damper_, lg4ff::kDamperSlot, damper, false);
}

bool Lg4ffDriver::set_friction(double friction) {
    return update(sent_friction_, lg4ff::kFrictionSlot, friction, model_.friction);
}

bool Lg4ffDriver::set_leds(std::uint32_t leds) {
    const auto mask = static_cast<std::uint8_t>(leds & lg4ff::kLedMask);
    if (!model_.leds || sent_leds_ == mask) {
        return true;
    }
    sent_leds_ = mask;
    return send(lg4ff::leds(mask));
}

bool Lg4ffDriver::apply_range(int degrees) {
    if (model_.range == lg4ff::Model::Range::g25) {
        return send(lg4ff::range(degrees));
    }
    bool sent = true;
    for (const Bytes& command : lg4ff::range_driving_force_pro(degrees)) {
        sent = send(command) && sent;
    }
    return sent;
}

int Lg4ffDriver::set_rotation(int degrees) {
    if (model_.range == lg4ff::Model::Range::none) {
        return 0;
    }
    degrees = std::clamp(degrees, lg4ff::kRangeLeast, lg4ff::kRangeMost);
    if (degrees == rotation_) {
        return rotation_;
    }
    if (!apply_range(degrees)) {
        wheel_log("the wheel refused its rotation");
        return 0;
    }
    rotation_ = degrees;
    rotation_changed_ = true;
    return rotation_;
}

void Lg4ffDriver::stop() {
    send(lg4ff::stop_all());
    constant_playing_ = false;
    sent_force_ = 0;
    sent_damper_ = lg4ff::damper(lg4ff::kDamperSlot, lg4ff::kStop, 0);
    sent_friction_ = lg4ff::damper(lg4ff::kFrictionSlot, lg4ff::kStop, 0);
}

void Lg4ffDriver::close() {
    stop();
    // Lights left lit go out as the wheel is let go.
    if (sent_leds_.value_or(0) != 0) {
        send(lg4ff::leds(0));
        sent_leds_.reset();
    }
    if (rotation_changed_) {
        apply_range(lg4ff::kRangeMost);
        rotation_changed_ = false;
        rotation_ = 0;
    }
}
