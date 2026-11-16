// What the thread plays each tick (wheel_player.h), on a fake clock.

#include "test.h"
#include "wheel_player.h"

#include <cmath>

namespace {

using Clock = WheelPlayer::Clock;
using std::chrono::milliseconds;

constexpr double kRate = 500.0;
constexpr milliseconds kTick{2};

// A player past its soft start, at `at` after it.
struct Started {
    WheelPlayer player;
    Clock::time_point now{};

    Started() {
        player.start(now, kRate);
        run(1100, 0.0);
    }

    // `ms` of ticks with the game setting `force` each one; the last frame.
    WheelFrame run(int ms, double force) {
        WheelFrame frame;
        for (int t = 0; t < ms; t += 2) {
            now += kTick;
            player.set_force(force, now);
            frame = player.step(now);
        }
        return frame;
    }
};

}  // namespace

TEST(player_ticks_at_the_wheels_rate) {
    // The thread plays as fast as each wheel keeps up: a T300's 125 updates
    // a second are a tick every 8 ms, HID++'s 250 one every 4.
    WheelPlayer player;
    player.start({}, 125.0);
    CHECK(player.period() == milliseconds(8));
    player.start({}, 250.0);
    CHECK(player.period() == milliseconds(4));
    player.start({}, 1000.0);
    CHECK(player.period() == milliseconds(1));
}

TEST(player_plays_the_steady_force) {
    Started wheel;
    CHECK(std::abs(wheel.run(100, 0.5).force - 0.5) < 1e-6);
    CHECK(std::abs(wheel.run(100, -0.3).force + 0.3) < 1e-6);
}

TEST(player_soft_clips_the_sum) {
    Started wheel;
    const double clipped = wheel.run(200, 1.5).force;
    CHECK(clipped < 1.0);
    CHECK(clipped > 0.95);
    // With a vibration on top the peaks are rounded, never past full scale.
    wheel.player.set_tone(0.3, 25.0, wheel.now);
    for (int i = 0; i < 200; ++i) {
        CHECK(std::abs(wheel.run(2, 0.9).force) < 1.0);
    }
}

TEST(player_adds_the_tone_and_the_texture) {
    Started wheel;
    wheel.player.set_tone(0.1, 25.0, wheel.now);
    wheel.player.set_texture(0.05, 40.0, wheel.now);
    double low = 1.0;
    double high = -1.0;
    for (int i = 0; i < 500; ++i) {
        const double force = wheel.run(2, 0.2).force;
        low = std::min(low, force);
        high = std::max(high, force);
    }
    // Both at once: past either alone, round the steady force.
    CHECK(high > 0.2 + 0.1);
    CHECK(low < 0.2 - 0.1);
}

TEST(player_eases_in_after_a_jump) {
    // After a teleport the force comes back over 0.4 s, from nothing.
    Started wheel;
    wheel.run(100, 0.8);
    const Clock::time_point jumped = wheel.now;
    wheel.player.ease_in(0.4, jumped);
    for (int t = 0; t < 400; t += 2) {
        const WheelFrame frame = wheel.run(2, 0.8);
        const double ramp = std::chrono::duration<double>(wheel.now - jumped).count() / 0.4;
        CHECK(std::abs(frame.force) <= 0.8 * ramp + 1e-9);
    }
    CHECK(std::abs(wheel.run(100, 0.8).force - 0.8) < 1e-6);
}

TEST(player_scales_the_damper_and_friction) {
    WheelPlayer player;
    Clock::time_point now{};
    player.start(now, kRate);
    player.set_damper(0.6, now);
    player.set_friction(3.0, now);  // clamped
    now += milliseconds(500);
    player.set_force(0.0, now);
    const WheelFrame half = player.step(now);  // halfway up the soft start
    CHECK(std::abs(half.damper - 0.3) < 1e-6);
    CHECK(std::abs(half.friction - 0.5) < 1e-6);
}

TEST(player_ignores_what_isnt_a_number) {
    Started wheel;
    wheel.player.set_force(std::nan(""), wheel.now);
    wheel.player.set_texture(std::nan(""), INFINITY, wheel.now);
    wheel.now += kTick;
    const WheelFrame frame = wheel.player.step(wheel.now);
    CHECK(std::isfinite(frame.force));
}

TEST(player_damps_the_steering_from_its_own_reading) {
    // The steering turning one way at a steady 0.5 axis units a second, damped
    // at 0.4 of full scale per unit a second: pushed back the other way, about
    // 0.2, from the position read here, not the game's.
    Started wheel;
    wheel.player.set_steering_damping(0.4, wheel.now);
    double position = 0.0;
    WheelFrame frame;
    for (int t = 0; t < 200; t += 2) {
        wheel.now += kTick;
        position += 0.5 * 0.002;
        wheel.player.set_position(position, wheel.now);
        wheel.player.set_force(0.0, wheel.now);
        frame = wheel.player.step(wheel.now);
    }
    CHECK(std::abs(frame.force + 0.2) < 0.01);
    // An axis that falls as the force turns it: the game sends the damping
    // negative, and it still pushes against the turning (it was dropped).
    wheel.player.set_steering_damping(-0.4, wheel.now);
    for (int t = 0; t < 200; t += 2) {
        wheel.now += kTick;
        position += 0.5 * 0.002;
        wheel.player.set_position(position, wheel.now);
        frame = wheel.player.step(wheel.now);
    }
    CHECK(std::abs(frame.force - 0.2) < 0.01);
    // Unreadable, there's no damping from here (the game damps instead).
    wheel.player.set_position(std::nullopt, wheel.now);
    wheel.now += kTick;
    CHECK(std::abs(wheel.player.step(wheel.now).force) < 1e-9);
}

TEST(player_corrects_the_spring_by_how_far_the_wheel_turned_since) {
    // The game's force was worked out for where the wheel was 40 ms ago;
    // turned on since by 0.05 at 4 of full scale per unit, the force is
    // that much less (pushed back): the spring near enough fresh.
    Started wheel;
    auto at = [&](double position) {
        wheel.now += kTick;
        wheel.player.set_position(position, wheel.now);
        wheel.player.set_force(0.2, wheel.now);
        return wheel.player.step(wheel.now).force;
    };
    for (int t = 0; t < 100; ++t) {
        at(0.0);
    }
    wheel.player.set_steering_spring(4.0, 0.04, wheel.now);
    CHECK(std::abs(at(0.0) - 0.2) < 0.01);  // not turned since: as the game said
    for (int t = 0; t < 4; ++t) {
        at(0.05);
    }
    CHECK(std::abs(at(0.05) - 0.0) < 0.02);  // 0.2 less 4 x 0.05
    // Read where it was 40 ms back: turned since the reading, not since the call.
    for (int t = 0; t < 100; ++t) {
        at(0.05);
    }
    wheel.player.set_steering_spring(4.0, 0.04, wheel.now);
    CHECK(std::abs(at(0.05) - 0.2) < 0.01);
    // A wild reference never throws the wheel: at most kMostSpring.
    wheel.player.set_steering_spring(400.0, 0.04, wheel.now);
    CHECK(at(0.9) > 0.2 - 0.5 - 0.01);
}

TEST(player_locks_the_column_from_its_own_reading) {
    // Locked at 0.1, free 0.02 either side, then 20 of full scale per unit
    // past it: turned 0.05 the one way, pushed back about 0.6 the other.
    Started wheel;
    wheel.player.set_column_lock(0.1, 0.02, 20.0, wheel.now);
    auto at = [&](double position) {
        wheel.now += kTick;
        wheel.player.set_position(position, wheel.now);
        wheel.player.set_force(0.0, wheel.now);
        return wheel.player.step(wheel.now).force;
    };
    for (int t = 0; t < 50; ++t) {
        at(0.1);  // still: no turning for the damping
    }
    CHECK(std::abs(at(0.11)) < 0.05);  // within the play, free
    for (int t = 0; t < 50; ++t) {
        at(0.15);
    }
    CHECK(at(0.15) < -0.4);
    for (int t = 0; t < 50; ++t) {
        at(0.05);
    }
    CHECK(at(0.05) > 0.4);  // and the other way
    // Freed (switched on), nothing.
    wheel.player.set_column_lock(NAN, 0.0, 0.0, wheel.now);
    CHECK(std::abs(at(0.05)) < 1e-9);
    // Unreadable, nothing from here either.
    wheel.player.set_column_lock(0.1, 0.02, 20.0, wheel.now);
    wheel.player.set_position(std::nullopt, wheel.now);
    wheel.now += kTick;
    CHECK(std::abs(wheel.player.step(wheel.now).force) < 1e-9);
}

TEST(player_damps_quick_turning_not_steady_steering) {
    // Quick damping only: a steady turn, as a hand steers, is barely held
    // back once it's going; a swing a few times a second is.
    Started wheel;
    wheel.player.set_quick_damping(0.4, wheel.now);
    double position = 0.0;
    WheelFrame frame;
    for (int t = 0; t < 2000; t += 2) {
        wheel.now += kTick;
        position += 0.5 * 0.002;
        wheel.player.set_position(position, wheel.now);
        wheel.player.set_force(0.0, wheel.now);  // the game still there
        frame = wheel.player.step(wheel.now);
    }
    CHECK(std::abs(frame.force) < 0.01);
    double most = 0.0;
    for (int t = 0; t < 1000; t += 2) {
        wheel.now += kTick;
        position = 0.05 * std::sin(2.0 * 3.14159265 * 3.0 * t / 1000.0);  // 3 swings a second
        wheel.player.set_position(position, wheel.now);
        wheel.player.set_force(0.0, wheel.now);
        most = std::max(most, std::abs(wheel.player.step(wheel.now).force));
    }
    // Calmed, but never with more than a share of the wheel's push: spun hand
    // over hand into a tight turn, the whole push fought the driver.
    CHECK(most > 0.08 && most <= 0.1 + 1e-9);
}

TEST(player_spring_pushes_back_towards_its_centre_whichever_way_the_axis_runs) {
    // Centred at 0.2, free 0.05 either side; above it 2 of full scale per unit up to 0.3, below
    // it 4 up to all of it.
    Started wheel;
    WheelSpring spring;
    spring.centre = 0.2;
    spring.deadband = 0.05;
    spring.positive_stiffness = 2.0;
    spring.negative_stiffness = 4.0;
    spring.positive_saturation = 0.3;
    spring.negative_saturation = 1.0;
    wheel.player.set_spring(spring, wheel.now);
    auto held = [&](double position) {
        WheelFrame frame;
        for (int t = 0; t < 50; ++t) {  // still: no turning for the damping
            wheel.now += kTick;
            wheel.player.set_position(position, wheel.now);
            wheel.player.set_force(0.0, wheel.now);
            frame = wheel.player.step(wheel.now);
        }
        return frame.force;
    };
    CHECK(std::abs(held(0.22)) < 1e-9);  // within the deadband
    // The axis falls as a positive force turns the wheel (every wheel Purr knows): above the
    // centre, a positive push brings it back, at most 0.3.
    CHECK(std::abs(held(0.4) - 0.3) < 0.02);
    CHECK(std::abs(held(0.0) + 0.6) < 0.05);  // below: 4 x 0.15 the other way
    // A wheel whose axis rises with the force: the same spring, pushed the other way.
    wheel.player.set_axis_direction(1);
    CHECK(std::abs(held(0.4) + 0.3) < 0.02);
    CHECK(std::abs(held(0.0) - 0.6) < 0.05);
    // Cleared, nothing.
    wheel.player.clear_spring(wheel.now);
    CHECK(std::abs(held(0.0)) < 1e-9);
}
