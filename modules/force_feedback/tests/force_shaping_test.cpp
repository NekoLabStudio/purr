// The steady force's shaping and the soft clip (force_shaping.h).

#include "force_shaping.h"
#include "test.h"

#include <cmath>

TEST(shaping_steps_without_overshoot) {
    // A step from nothing to full: there by 95% within 12 ms, never past it,
    // at the thread's rates.
    for (const double tick : {0.001, 0.002, 0.0025}) {
        ForceShaping shaping;
        shaping.reset();
        double out = 0.0;
        double at_95 = -1.0;
        double last = 0.0;
        for (double t = tick; t < 0.1; t += tick) {
            out = shaping.step(1.0, tick);
            CHECK(out <= 1.0);
            CHECK(out >= last);  // monotonic: no ringing
            last = out;
            if (at_95 < 0.0 && out >= 0.95) {
                at_95 = t;
            }
        }
        CHECK(at_95 > 0.0 && at_95 <= 0.012 + 1e-9);
    }
}

TEST(shaping_passes_a_steady_force_unchanged) {
    ForceShaping shaping;
    shaping.reset(0.37);
    for (int i = 0; i < 100; ++i) {
        CHECK_EQ(shaping.step(0.37, 0.002), 0.37);
    }
    // And settles on one it's given.
    shaping.reset();
    double out = 0.0;
    for (int i = 0; i < 500; ++i) {
        out = shaping.step(-0.6, 0.002);
    }
    CHECK(std::abs(out + 0.6) < 1e-9);
}

TEST(shaping_limits_the_slew) {
    // No more than full scale per 10 ms, however hard the physics spikes.
    ForceShaping shaping;
    shaping.reset();
    double last = 0.0;
    for (int i = 0; i < 200; ++i) {
        const double target = (i / 10) % 2 == 0 ? 4.0 : -4.0;
        const double out = shaping.step(target, 0.002);
        CHECK(std::abs(out - last) <= 0.2 + 1e-12);
        last = out;
    }
}

TEST(soft_clip_rounds_off_past_the_knee) {
    // The identity up to 0.8, monotonic, continuous, and short of full scale.
    for (double x = -0.8; x <= 0.8; x += 0.01) {
        CHECK_EQ(soft_clip(x), x);
    }
    double last = soft_clip(-3.0);
    for (double x = -3.0; x <= 3.0; x += 0.001) {
        const double y = soft_clip(x);
        CHECK(y >= last);
        CHECK(std::abs(y - last) < 0.0011);
        CHECK(std::abs(y) < 1.0);
        last = y;
    }
    CHECK(soft_clip(1.5) < 1.0);
    CHECK(soft_clip(1.0) > 0.95);
    CHECK_EQ(soft_clip(-1.2), -soft_clip(1.2));
}
