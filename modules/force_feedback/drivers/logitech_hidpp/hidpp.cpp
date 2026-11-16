#include "hidpp.h"

#include "../../wheel_log.h"

#include <string>

namespace hidpp {

namespace {

// The kernel's own software ID (LINUX_KERNEL_SW_ID): replies echo it.
constexpr std::uint8_t kSoftwareId = 0x01;
constexpr std::uint8_t kDeviceIndex = 0xFF;
// Error replies put one of these where the feature index goes: HID++ 1.0's
// and 2.0's (HIDPP_ERROR, HIDPP20_ERROR).
constexpr std::uint8_t kError10 = 0x8F;
constexpr std::uint8_t kError20 = 0xFF;

// Root (feature index 0).
constexpr std::uint8_t kGetFeature = 0;
// ForceFeedback's functions.
constexpr std::uint8_t kGetInfo = 0;
constexpr std::uint8_t kResetAll = 1;
constexpr std::uint8_t kDownloadEffect = 2;
constexpr std::uint8_t kSetEffectState = 3;
constexpr std::uint8_t kGetAperture = 5;
constexpr std::uint8_t kSetAperture = 6;
constexpr std::uint8_t kSetGlobalGains = 8;

constexpr std::uint8_t kPlay = 0x02;
constexpr std::uint8_t kConstant = 0x00;
constexpr std::uint8_t kSpring = 0x06;
constexpr std::uint8_t kDamper = 0x07;
constexpr std::uint8_t kFriction = 0x08;
constexpr std::uint8_t kAutostart = 0x80;

// A wheel answers in a few milliseconds; the game waits on open, so not long.
constexpr auto kReplyTimeout = std::chrono::milliseconds(250);

std::uint8_t function_byte(std::uint8_t function) {
    return static_cast<std::uint8_t>(function << 4 | kSoftwareId);
}

// A condition effect's saturation, coefficient, dead band and centre, as
// hidpp_ff_upload_effect packs them: saturations as 15 bits, the rest as
// big-endian 16-bit numbers.
Bytes condition(std::uint8_t slot, std::uint8_t type, int coefficient) {
    const auto hi = static_cast<std::uint8_t>((coefficient >> 8) & 0xFF);
    const auto lo = static_cast<std::uint8_t>(coefficient & 0xFF);
    const std::uint16_t saturation = coefficient == 0 ? 0 : 0xFFFF;
    const auto sat_hi = static_cast<std::uint8_t>(saturation >> 9);
    const auto sat_lo = static_cast<std::uint8_t>((saturation >> 1) & 0xFF);
    return {slot, type, 0x00, 0x00, 0x00, 0x00, sat_hi, sat_lo, hi, lo, 0x00, 0x00, 0x00, 0x00, hi, lo, sat_hi, sat_lo};
}

}  // namespace

Bytes request(std::uint8_t feature_index, std::uint8_t function, const Bytes& params, std::size_t very_long_length) {
    const bool long_enough = params.size() <= kLongParams;
    if (!long_enough && very_long_length < params.size() + 4) {
        return {};
    }
    Bytes report{long_enough ? kLongReport : kVeryLongReport, kDeviceIndex, feature_index, function_byte(function)};
    report.insert(report.end(), params.begin(), params.end());
    report.resize(long_enough ? kLongLength : very_long_length, 0x00);
    return report;
}

Bytes constant_effect(std::uint8_t slot, int level) {
    const auto hi = static_cast<std::uint8_t>((level >> 8) & 0xFF);
    const auto lo = static_cast<std::uint8_t>(level & 0xFF);
    // Slot, type, duration (0 is endless), delay, force, then an envelope of nothing.
    return {slot, kConstant | kAutostart, 0x00, 0x00, 0x00, 0x00, hi, lo, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
}

Bytes damper_effect(std::uint8_t slot, int coefficient) {
    return condition(slot, kDamper | kAutostart, coefficient);
}

Bytes friction_effect(std::uint8_t slot, int coefficient) {
    return condition(slot, kFriction | kAutostart, coefficient);
}

Bytes no_spring() {
    return condition(0, kSpring | kAutostart, 0);
}

}  // namespace hidpp

std::optional<Bytes> HidppDriver::call(std::uint8_t feature_index, std::uint8_t function, const Bytes& params,
                                       const char* what) {
    const Bytes sent = hidpp::request(feature_index, function, params, very_long_length_);
    if (!send(sent)) {
        wheel_log(std::string("HID++ ") + what + ": the write failed");
        return std::nullopt;
    }
    const auto deadline = std::chrono::steady_clock::now() + hidpp::kReplyTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        const std::optional<Bytes> reply = transport_.next_input(left);
        if (!reply) {
            break;
        }
        const Bytes& r = *reply;
        if (r.size() < 7 || (r[0] != hidpp::kLongReport && r[0] != hidpp::kVeryLongReport)) {
            continue;
        }
        if (r[2] == sent[2] && r[3] == sent[3]) {
            return Bytes(r.begin() + 4, r.end());
        }
        if ((r[2] == hidpp::kError10 || r[2] == hidpp::kError20) && r[3] == sent[2] && r[4] == sent[3]) {
            wheel_log(std::string("HID++ ") + what + ": the wheel answered with error " + std::to_string(r[5]));
            return std::nullopt;
        }
    }
    wheel_log(std::string("HID++ ") + what + ": no answer");
    return std::nullopt;
}

bool HidppDriver::send(const Bytes& report) {
    return !report.empty() && transport_.send(ReportType::output, report[0], report);
}

bool HidppDriver::post(std::uint8_t function, const Bytes& params) {
    count_errors();
    return send(hidpp::request(feature_, function, params, very_long_length_));
}

void HidppDriver::count_errors() {
    while (const std::optional<Bytes> reply = transport_.next_input(std::chrono::milliseconds(0))) {
        const Bytes& r = *reply;
        if (r.size() >= 7 && (r[2] == hidpp::kError10 || r[2] == hidpp::kError20) && r[3] == feature_) {
            if (errors_++ == 0) {
                wheel_log("HID++: the wheel answered an update with error " + std::to_string(r[5]));
            }
        }
    }
}

std::optional<std::uint8_t> HidppDriver::download(const Bytes& params, const char* what) {
    const std::optional<Bytes> slot = call(feature_, hidpp::kDownloadEffect, params, what);
    if (!slot || (*slot)[0] == 0) {
        return std::nullopt;
    }
    return (*slot)[0];
}

bool HidppDriver::open() {
    errors_ = 0;
    next_ = 0;
    for (Pending& one : pending_) {
        one = {};
    }
    for (std::uint8_t& slot : slots_) {
        slot = 0;
    }
    rotation_ = rotation_before_ = 0;
    rotation_changed_ = false;
    const std::optional<Bytes> feature =
        call(0, hidpp::kGetFeature,
             {static_cast<std::uint8_t>(hidpp::kForceFeedbackFeature >> 8), hidpp::kForceFeedbackFeature & 0xFF},
             "finding the force feedback feature");
    if (!feature || (*feature)[0] == 0) {
        wheel_log("HID++: the wheel has no force feedback feature (0x8123)");
        return false;
    }
    feature_ = (*feature)[0];
    if (const std::optional<Bytes> info = call(feature_, hidpp::kGetInfo, {}, "GetInfo")) {
        wheel_log("HID++: force feedback at feature index " + std::to_string(feature_) + ", " +
                  std::to_string((*info)[0]) + " effect slots");
    }
    // As g920_get_config and hidpp_ff_init start the wheel: everything reset,
    // its rotation read, its gain full, a spring of nothing in place of its
    // own centring.
    call(feature_, hidpp::kResetAll, {}, "ResetAll");
    if (const std::optional<Bytes> aperture = call(feature_, hidpp::kGetAperture, {}, "GetAperture")) {
        rotation_ = rotation_before_ = (*aperture)[0] << 8 | (*aperture)[1];
    }
    call(feature_, hidpp::kSetGlobalGains, {0xFF, 0xFF, 0x00, 0x00}, "SetGlobalGains");
    if (very_long_length_ == 0) {
        wheel_log("HID++: the wheel has no very long report (0x12), so no damper or friction and its own centring stays");
    } else {
        call(feature_, hidpp::kDownloadEffect, hidpp::no_spring(), "taking its centring off");
    }
    if (!start_effects()) {
        // As it was: everything off and its own centring back.
        post(hidpp::kResetAll, {});
        return false;
    }
    return true;
}

bool HidppDriver::start_effects() {
    const std::optional<std::uint8_t> constant = download(hidpp::constant_effect(0, 0), "downloading the constant force");
    if (!constant) {
        return false;
    }
    slots_[force] = *constant;
    if (very_long_length_ != 0) {
        const std::optional<std::uint8_t> damping = download(hidpp::damper_effect(0, 0), "downloading the damper");
        if (!damping) {
            return false;
        }
        slots_[damper] = *damping;
        if (has_friction_) {
            const std::optional<std::uint8_t> rubbing = download(hidpp::friction_effect(0, 0), "downloading friction");
            slots_[friction] = rubbing.value_or(0);
            if (!rubbing) {
                wheel_log("HID++: the wheel took no friction effect, so it plays none");
            }
        }
    }
    // The G920 starts an effect on SetEffectState(PLAY), as the kernel sends
    // it; the G923 for Xbox ignores that and starts on the autostart bit.
    for (const std::uint8_t slot : slots_) {
        if (slot != 0 && !post(hidpp::kSetEffectState, {slot, hidpp::kPlay})) {
            return false;
        }
    }
    return true;
}

// Downloading into a slot that holds an effect changes it in place, as the
// kernel does for every update (hidpp_ff_find_effect); no reply is awaited.
bool HidppDriver::send_effect(Effect effect) {
    Pending& one = pending_[effect];
    one.sent = one.wanted;
    switch (effect) {
    case force:
        return post(hidpp::kDownloadEffect, hidpp::constant_effect(slots_[force], one.wanted));
    case damper:
        return post(hidpp::kDownloadEffect, hidpp::damper_effect(slots_[damper], one.wanted));
    case friction:
        return post(hidpp::kDownloadEffect, hidpp::friction_effect(slots_[friction], one.wanted));
    case effects:
        break;
    }
    return true;
}

bool HidppDriver::set_force(double force_share) {
    pending_[force].wanted = steps(force_share, 32767);
    return pending_[force].wanted == pending_[force].sent || send_effect(force);
}

bool HidppDriver::set_damper(double damper_share) {
    pending_[damper].wanted = steps(damper_share, 32767, 0.0);
    return slots_[damper] == 0 || pending_[damper].wanted == pending_[damper].sent || send_effect(damper);
}

bool HidppDriver::set_friction(double friction_share) {
    pending_[friction].wanted = steps(friction_share, 32767, 0.0);
    return slots_[friction] == 0 || pending_[friction].wanted == pending_[friction].sent || send_effect(friction);
}

bool HidppDriver::play(double force_share, double damper_share, double friction_share) {
    pending_[force].wanted = steps(force_share, 32767);
    pending_[damper].wanted = steps(damper_share, 32767, 0.0);
    pending_[friction].wanted = steps(friction_share, 32767, 0.0);
    for (int turn = 0; turn < effects; ++turn) {
        const auto effect = static_cast<Effect>((next_ + turn) % effects);
        const Pending& one = pending_[effect];
        if (slots_[effect] != 0 && one.wanted != one.sent) {
            next_ = (effect + 1) % effects;
            return send_effect(effect);
        }
    }
    return true;
}

int HidppDriver::set_rotation(int degrees) {
    degrees = std::clamp(degrees, kRangeLeast, kRangeMost);
    if (degrees == rotation_) {
        return rotation_;
    }
    if (!call(feature_, hidpp::kSetAperture,
              {static_cast<std::uint8_t>(degrees >> 8), static_cast<std::uint8_t>(degrees & 0xFF)}, "SetAperture")) {
        return 0;
    }
    rotation_ = degrees;
    rotation_changed_ = true;
    return rotation_;
}

void HidppDriver::stop() {
    set_force(0.0);
    set_damper(0.0);
    set_friction(0.0);
}

void HidppDriver::close() {
    stop();
    // Its rotation as it was, or as the kernel takes it when it can't be read.
    if (rotation_changed_) {
        const int before = rotation_before_ != 0 ? rotation_before_ : kRangeMost;
        call(feature_, hidpp::kSetAperture,
             {static_cast<std::uint8_t>(before >> 8), static_cast<std::uint8_t>(before & 0xFF)}, "SetAperture");
        rotation_changed_ = false;
    }
    // Everything off, its own centring back.
    post(hidpp::kResetAll, {});
    count_errors();
    if (errors_ > 1) {
        wheel_log("HID++: " + std::to_string(errors_) + " updates answered with errors");
    }
}
