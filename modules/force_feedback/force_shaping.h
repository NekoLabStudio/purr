#pragma once

// The steady force on its way to the wheel, and the last word on what it
// plays. The game sets the force once a physics step (120 Hz) while the
// thread plays a few hundred times a second, so held as it comes a
// direct-drive base plays a grainy 120 Hz staircase. ForceShaping rounds
// the steps off with two lags of kTau in a row (a critically damped
// filter: a step never overshoots, about 4 ms of delay, under half a
// physics step), then lets the force move no faster than full scale per
// kSlewTime, so a spike from the physics can't throw a strong base while
// a kerb still kicks within a few milliseconds. The vibrations are added
// after it (vibration.h), and soft_clip rounds off whatever the sum asks
// past the wheel's strongest instead of cutting it flat.

#include <algorithm>
#include <cmath>

class ForceShaping {
public:
    static constexpr double kTau = 0.002;       // seconds
    static constexpr double kSlewTime = 0.010;  // seconds for full scale

    // Holding `value`, as the wheel opens (nothing).
    void reset(double value = 0.0) { first_ = second_ = out_ = value; }

    // The force after `seconds` more of `target`.
    double step(double target, double seconds) {
        if (seconds <= 0.0) {
            return out_;
        }
        const double k = 1.0 - std::exp(-seconds / kTau);
        first_ += (target - first_) * k;
        second_ += (first_ - second_) * k;
        const double most = seconds / kSlewTime;
        out_ += std::clamp(second_ - out_, -most, most);
        return out_;
    }

private:
    double first_ = 0.0;
    double second_ = 0.0;
    double out_ = 0.0;
};

// Unchanged up to kKnee of full scale; beyond it, eased towards full scale
// and never past it, with no corner at the knee.
inline double soft_clip(double force) {
    constexpr double kKnee = 0.8;
    constexpr double kRoom = 1.0 - kKnee;
    const double size = std::abs(force);
    if (size <= kKnee) {
        return force;
    }
    return std::copysign(kKnee + kRoom * std::tanh((size - kKnee) / kRoom), force);
}
