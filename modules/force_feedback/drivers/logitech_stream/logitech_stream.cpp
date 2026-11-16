#include "logitech_stream.h"

#include "../../wheel_log.h"

#include <cmath>
#include <string>

namespace logitech_stream {

namespace {

constexpr std::uint8_t kDeviceIndex = 0xFF;
constexpr std::uint8_t kSoftwareId = 0x0A;
constexpr std::uint8_t kStream = 0x01;
constexpr std::uint8_t kLongReport = 0x11;
constexpr std::uint8_t kVeryLongReport = 0x12;
constexpr std::uint8_t kError10 = 0x8F;
constexpr std::uint8_t kError20 = 0xFF;
constexpr std::uint8_t kGetFeature = 0;
constexpr std::uint8_t kGetRotation = 1;
constexpr std::uint8_t kSetRotation = 2;
constexpr auto kReplyTimeout = std::chrono::milliseconds(250);
// With no report for this long the wheel is taken as still: a wheel moving
// reports every millisecond.
constexpr auto kStill = std::chrono::milliseconds(10);
// The rotation taken when it can't be read.
constexpr int kRotationUnknown = 900;

std::uint8_t function_byte(std::uint8_t function) {
    return static_cast<std::uint8_t>(function << 4 | kSoftwareId);
}

}  // namespace

std::uint16_t force_value(double force) {
    return static_cast<std::uint16_t>(0x8000 - steps(force, 32767));
}

Bytes force_packet(std::uint8_t sequence, double force) {
    const std::uint16_t value = force_value(force);
    Bytes packet(kForceLength, 0x00);
    packet[0] = kForceReport;
    packet[4] = kStream;
    packet[5] = sequence;
    packet[6] = packet[8] = static_cast<std::uint8_t>(value & 0xFF);
    packet[7] = packet[9] = static_cast<std::uint8_t>(value >> 8);
    return packet;
}

Bytes request(std::uint8_t feature_index, std::uint8_t function, const Bytes& params) {
    Bytes report = {kShortReport, kDeviceIndex, feature_index, function_byte(function)};
    report.insert(report.end(), params.begin(), params.end());
    report.resize(7, 0x00);
    return report;
}

std::optional<double> position(const Bytes& report) {
    if (report.size() < 6) {
        return std::nullopt;
    }
    const int raw = report[4] | (report[5] << 8);
    return (raw - 0x8000) / 32768.0;
}

}  // namespace logitech_stream

using namespace logitech_stream;

std::optional<Bytes> LogitechStreamDriver::call(std::uint8_t feature_index, std::uint8_t function,
                                                const Bytes& params) {
    if (!hidpp_) {
        return std::nullopt;
    }
    const Bytes sent = request(feature_index, function, params);
    if (!hidpp_->send(ReportType::output, kShortReport, sent)) {
        return std::nullopt;
    }
    const auto deadline = Clock::now() + kReplyTimeout;
    while (Clock::now() < deadline) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        const std::optional<Bytes> reply = hidpp_->next_input(left);
        if (!reply) {
            break;
        }
        const Bytes& r = *reply;
        if (r.size() < 7 || (r[0] != kLongReport && r[0] != kVeryLongReport) || r[1] != kDeviceIndex) {
            continue;
        }
        if (r[2] == sent[2] && r[3] == sent[3]) {
            return Bytes(r.begin() + 4, r.end());
        }
        if ((r[2] == kError10 || r[2] == kError20) && r[3] == sent[2] && r[4] == sent[3]) {
            wheel_log("HID++ (direct drive): the wheel answered with error " + std::to_string(r[5]));
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<int> LogitechStreamDriver::read_rotation() {
    const std::optional<Bytes> range = call(rotation_feature_, kGetRotation, {});
    if (!range || range->size() < 2) {
        return std::nullopt;
    }
    return (*range)[0] << 8 | (*range)[1];
}

bool LogitechStreamDriver::open() {
    sequence_ = 0;
    force_ = damper_ = friction_ = speed_ = 0.0;
    angle_.reset();
    rotation_feature_ = 0;
    rotation_ = rotation_before_ = 0;
    rotation_changed_ = false;
    if (const std::optional<Bytes> feature = call(0, kGetFeature,
                                                              {static_cast<std::uint8_t>(kRotationFeature >> 8),
                                                               static_cast<std::uint8_t>(kRotationFeature & 0xFF)})) {
        rotation_feature_ = (*feature)[0];
    }
    if (rotation_feature_ != 0) {
        rotation_ = rotation_before_ = read_rotation().value_or(0);
    } else {
        wheel_log("direct drive: no HID++ rotation (0x8138), so its rotation stays as set on the wheel");
    }
    if (!input_) {
        wheel_log("direct drive: the wheel's position isn't readable, so no damper or friction");
    }
    if (!send_force(0.0)) {
        wheel_log("the wheel refused its force stream");
        return false;
    }
    return true;
}

bool LogitechStreamDriver::send_force(double force) {
    return forces_.send(ReportType::output, kForceReport, force_packet(sequence_++, force));
}

bool LogitechStreamDriver::set_force(double force) {
    force_ = force;
    return send_force(force);
}

bool LogitechStreamDriver::set_damper(double damper) {
    damper_ = std::clamp(damper, 0.0, 1.0);
    return true;
}

bool LogitechStreamDriver::set_friction(double friction) {
    friction_ = std::clamp(friction, 0.0, 1.0);
    return true;
}

void LogitechStreamDriver::follow_wheel() {
    if (!input_) {
        return;
    }
    std::optional<double> latest;
    while (const std::optional<Bytes> report = input_->next_input(std::chrono::milliseconds(0))) {
        if (const std::optional<double> at = position(*report)) {
            latest = at;
        }
    }
    const Clock::time_point now = clock_();
    if (!angle_) {
        if (latest) {
            angle_ = *latest * (rotation_ > 0 ? rotation_ : kRotationUnknown) / 2.0;
            angle_at_ = now;
        }
        return;
    }
    const double seconds = std::chrono::duration<double>(now - angle_at_).count();
    if (seconds <= 0.0) {
        return;
    }
    double speed = 0.0;
    if (latest) {
        const double degrees = *latest * (rotation_ > 0 ? rotation_ : kRotationUnknown) / 2.0;
        speed = (degrees - *angle_) / seconds;
        angle_ = degrees;
        angle_at_ = now;
    } else if (now - angle_at_ < kStill) {
        return;  // between reports
    }
    speed_ += (speed - speed_) * (1.0 - std::exp(-seconds / kSpeedSmoothing));
}

bool LogitechStreamDriver::play(double force, double damper, double friction) {
    force_ = force;
    set_damper(damper);
    set_friction(friction);
    follow_wheel();
    // Turning right, both push left (positive), and the other way about.
    const double resisting = damper_ * std::clamp(speed_ / kDamperFullSpeed, -1.0, 1.0) +
                             friction_ * std::clamp(speed_ / kFrictionFullSpeed, -1.0, 1.0);
    return send_force(std::clamp(force_ + resisting, -1.0, 1.0));
}

int LogitechStreamDriver::set_rotation(int degrees) {
    if (rotation_feature_ == 0) {
        return 0;
    }
    degrees = std::clamp(degrees, kRangeLeast, kRangeMost);
    if (degrees == rotation_) {
        return rotation_;
    }
    if (!call(rotation_feature_, kSetRotation,
              {static_cast<std::uint8_t>(degrees >> 8), static_cast<std::uint8_t>(degrees & 0xFF), 0x00})) {
        wheel_log("direct drive: the wheel refused its rotation");
        return 0;
    }
    // In its onboard modes it keeps its own: what it says is what it is.
    rotation_ = read_rotation().value_or(degrees);
    rotation_changed_ = true;
    // Its position means other degrees now: follow it afresh.
    angle_.reset();
    speed_ = 0.0;
    if (rotation_ != degrees) {
        wheel_log("direct drive: the wheel kept its rotation at " + std::to_string(rotation_) +
                  " degrees (set it to desktop mode to let the game set it)");
    }
    return rotation_;
}

void LogitechStreamDriver::stop() {
    damper_ = friction_ = 0.0;
    set_force(0.0);
}

void LogitechStreamDriver::close() {
    stop();
    if (rotation_changed_ && rotation_before_ > 0 && rotation_before_ != rotation_) {
        call(rotation_feature_, kSetRotation,
             {static_cast<std::uint8_t>(rotation_before_ >> 8), static_cast<std::uint8_t>(rotation_before_ & 0xFF), 0x00});
    }
    rotation_changed_ = false;
}
