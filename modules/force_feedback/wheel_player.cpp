#include "wheel_player.h"

#include <algorithm>
#include <cmath>

namespace {

// The most of full scale the steady force may ask: past full scale it's
// soft-clipped, and beyond this it's surely a fault.
constexpr double kMostForce = 4.0;

// What the game sent, or nothing if it isn't a number.
double or_nothing(double value) {
    return std::isfinite(value) ? value : 0.0;
}

// A spring's push at `position`, towards its centre: `falls` is 1 where a positive force moves the
// axis down, -1 where it moves it up.
double spring_push(const WheelSpring& spring, double position, double falls) {
    const double offset = position - spring.centre;
    const double past = std::max(std::abs(offset) - spring.deadband, 0.0);
    const bool above = offset > 0.0;
    const double stiffness = above ? spring.positive_stiffness : spring.negative_stiffness;
    const double most = std::clamp(above ? spring.positive_saturation : spring.negative_saturation, 0.0, 1.0);
    return falls * std::copysign(std::clamp(stiffness * past, -most, most), offset);
}

}  // namespace

void WheelPlayer::start(Clock::time_point now, double rate) {
    rate_ = rate;
    force_ = damper_ = friction_ = steering_damping_ = quick_damping_ = turning_ = turning_slow_ = 0.0;
    lock_.reset();
    spring_setting_.reset();
    spring_stiffness_ = spring_ = 0.0;
    spring_at_.reset();
    history_next_ = history_count_ = 0;
    position_.reset();
    last_position_.reset();
    shaping_.reset();
    tone_.start(rate);
    texture_.start(rate);
    level_.start(now);
    last_step_ = now;
}

void WheelPlayer::set_force(double force, Clock::time_point now) {
    force_ = std::clamp(or_nothing(force), -kMostForce, kMostForce);
    level_.touch(now);
}

void WheelPlayer::set_tone(double amplitude, double frequency, Clock::time_point now) {
    tone_.set(or_nothing(amplitude), or_nothing(frequency));
    level_.touch(now);
}

void WheelPlayer::set_texture(double amplitude, double frequency, Clock::time_point now) {
    texture_.set(or_nothing(amplitude), or_nothing(frequency));
    level_.touch(now);
}

void WheelPlayer::set_damper(double damper, Clock::time_point now) {
    damper_ = or_nothing(damper);
    level_.touch(now);
}

void WheelPlayer::set_friction(double friction, Clock::time_point now) {
    friction_ = or_nothing(friction);
    level_.touch(now);
}

void WheelPlayer::set_steering_damping(double damping, Clock::time_point now) {
    steering_damping_ = or_nothing(damping);
    level_.touch(now);
}

void WheelPlayer::set_quick_damping(double damping, Clock::time_point now) {
    quick_damping_ = or_nothing(damping);
    level_.touch(now);
}

void WheelPlayer::set_column_lock(double at, double play, double stiffness, Clock::time_point now) {
    level_.touch(now);
    if (!std::isfinite(at)) {
        lock_.reset();  // NAN: free
        return;
    }
    // Signed as the damping is: negative where the axis falls as a positive force turns it.
    stiffness = or_nothing(stiffness);
    WheelSpring lock;
    lock.centre = at;
    lock.deadband = std::max(or_nothing(play), 0.0);
    lock.positive_stiffness = lock.negative_stiffness = std::abs(stiffness);
    lock.positive_saturation = lock.negative_saturation = kMostLock;
    lock_ = lock;
    lock_falls_ = stiffness < 0.0 ? 1.0 : -1.0;
}

void WheelPlayer::set_spring(const WheelSpring& spring, Clock::time_point now) {
    level_.touch(now);
    if (!std::isfinite(spring.centre)) {
        spring_setting_.reset();
        return;
    }
    WheelSpring checked;
    checked.centre = std::clamp(spring.centre, -1.0, 1.0);
    checked.deadband = std::max(or_nothing(spring.deadband), 0.0);
    checked.positive_stiffness = or_nothing(spring.positive_stiffness);
    checked.negative_stiffness = or_nothing(spring.negative_stiffness);
    checked.positive_saturation = std::clamp(or_nothing(spring.positive_saturation), 0.0, 1.0);
    checked.negative_saturation = std::clamp(or_nothing(spring.negative_saturation), 0.0, 1.0);
    spring_setting_ = checked;
}

void WheelPlayer::clear_spring(Clock::time_point now) {
    level_.touch(now);
    spring_setting_.reset();
}

void WheelPlayer::set_steering_spring(double stiffness, double lag, Clock::time_point now) {
    spring_stiffness_ = or_nothing(stiffness);
    spring_at_ = position_ago(std::clamp(or_nothing(lag), 0.0, kLongestLag), now);
    level_.touch(now);
}

void WheelPlayer::set_position(std::optional<double> position, Clock::time_point now) {
    position_ = position && std::isfinite(*position) ? position : std::nullopt;
    if (position_) {
        history_[history_next_] = {now, *position_};
        history_next_ = (history_next_ + 1) % kHistory;
        history_count_ = std::min(history_count_ + 1, kHistory);
    } else {
        history_count_ = 0;
    }
}

std::optional<double> WheelPlayer::position_ago(double seconds, Clock::time_point now) const {
    if (history_count_ == 0) {
        return std::nullopt;
    }
    const Clock::time_point wanted = now - std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
    // Newest first, the first reading at or before then (the oldest kept if none is).
    std::optional<double> found;
    for (std::size_t k = 1; k <= history_count_; ++k) {
        const auto& [at, position] = history_[(history_next_ + kHistory - k) % kHistory];
        found = position;
        if (at <= wanted) {
            break;
        }
    }
    return found;
}

void WheelPlayer::ease_in(double seconds, Clock::time_point now) {
    level_.ease_in(now, std::chrono::duration<double>(or_nothing(seconds)));
}

WheelFrame WheelPlayer::step(Clock::time_point now) {
    const double seconds = std::clamp(std::chrono::duration<double>(now - last_step_).count(), 0.0, kLongestStep);
    last_step_ = now;
    const double level = level_.step(now);
    const double steady = shaping_.step(force_, seconds);
    double damping = 0.0;
    double column = 0.0;
    double spring = 0.0;
    if (position_) {
        if (spring_at_) {
            spring = std::clamp(-spring_stiffness_ * (*position_ - *spring_at_), -kMostSpring, kMostSpring);
        }
        if (lock_) {
            column = spring_push(*lock_, *position_, lock_falls_);
        }
        if (spring_setting_) {
            column += spring_push(*spring_setting_, *position_, axis_falls_);
        }
        if (last_position_ && seconds > 0.0) {
            const double now_turning = (*position_ - *last_position_) / seconds;
            turning_ += (now_turning - turning_) * (1.0 - std::exp(-seconds / kTurningTau));
            turning_slow_ += (turning_ - turning_slow_) * (1.0 - std::exp(-seconds / kSlowTau));
        }
        last_position_ = position_;
        const double quick = std::clamp(-quick_damping_ * (turning_ - turning_slow_), -kMostQuick, kMostQuick);
        damping = std::clamp(-steering_damping_ * turning_ + quick, -kMostDamping, kMostDamping);
        damping_ = damping;
    } else {
        damping_ = 0.0;
        last_position_.reset();
        turning_ = turning_slow_ = 0.0;
    }
    WheelFrame frame;
    spring_ = spring;
    frame.force = soft_clip(steady + spring + damping + column + tone_.step() + texture_.step()) * level;
    frame.damper = std::clamp(damper_, 0.0, 1.0) * level;
    frame.friction = std::clamp(friction_, 0.0, 1.0) * level;
    return frame;
}

WheelPlayer::Clock::duration WheelPlayer::period() const {
    return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / rate_));
}
