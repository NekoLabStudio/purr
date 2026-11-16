#pragma once

// How much of what the game asks for reaches the wheel, 0 to 1: the soft
// start and the watchdog. A wheel that has just opened ramps up from nothing
// over kRampUp, so it never jerks out of the player's hands. If the game
// stops setting anything for kStale (a hang, a debugger, a long stall), the
// wheel fades to nothing over kFadeOut instead of holding its last push;
// the game sets every physics step, even while paused, so in play it never
// fades. Once the game sets again it ramps back up as from a start. After
// a jump (a teleport, a reset) the game eases it in again from nothing,
// quicker than a start.

#include <algorithm>
#include <chrono>

class OutputLevel {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr std::chrono::milliseconds kRampUp{1000};
    static constexpr std::chrono::milliseconds kStale{500};
    static constexpr std::chrono::milliseconds kFadeOut{250};

    // From nothing, as the wheel opens.
    void start(Clock::time_point now) {
        level_ = 0.0;
        ramp_ = kRampUp;
        last_set_ = last_step_ = now;
    }

    // From nothing again, up over `ramp`.
    void ease_in(Clock::time_point now, std::chrono::duration<double> ramp) {
        level_ = 0.0;
        ramp_ = std::max(ramp, std::chrono::duration<double>(0.001));
        last_set_ = now;
    }

    // The game set something.
    void touch(Clock::time_point now) { last_set_ = now; }

    // The level at `now`.
    double step(Clock::time_point now) {
        const double seconds = std::chrono::duration<double>(now - last_step_).count();
        last_step_ = now;
        if (now - last_set_ < kStale) {
            level_ = std::min(1.0, level_ + seconds / ramp_.count());
        } else {
            level_ = std::max(0.0, level_ - seconds / std::chrono::duration<double>(kFadeOut).count());
            ramp_ = kRampUp;
        }
        return level_;
    }

private:
    double level_ = 0.0;
    std::chrono::duration<double> ramp_ = kRampUp;
    Clock::time_point last_set_{};
    Clock::time_point last_step_{};
};
