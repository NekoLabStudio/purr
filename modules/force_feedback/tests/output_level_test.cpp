#include "output_level.h"
#include "test.h"

#include <cmath>

namespace {

using Clock = OutputLevel::Clock;
using std::chrono::milliseconds;

bool near(double a, double b) {
    return std::abs(a - b) < 1e-6;
}

}  // namespace

TEST(soft_start_ramps_up_over_a_second) {
    OutputLevel level;
    const Clock::time_point start{};
    level.start(start);
    CHECK(near(level.step(start), 0.0));
    for (int ms = 100; ms <= 1500; ms += 100) {
        const auto now = start + milliseconds(ms);
        level.touch(now);
        const double got = level.step(now);
        CHECK(near(got, std::min(1.0, ms / 1000.0)));
    }
}

TEST(watchdog_fades_a_stalled_game_to_nothing) {
    OutputLevel level;
    const Clock::time_point start{};
    level.start(start);
    for (int ms = 10; ms <= 1200; ms += 10) {
        level.touch(start + milliseconds(ms));
        level.step(start + milliseconds(ms));
    }
    // The game stops setting at 1.2 s: full until it has been quiet for half
    // a second, then down to nothing over a quarter of one.
    CHECK(near(level.step(start + milliseconds(1690)), 1.0));
    CHECK(near(level.step(start + milliseconds(1825)), 0.46));
    CHECK(near(level.step(start + milliseconds(2000)), 0.0));
    CHECK(near(level.step(start + milliseconds(5000)), 0.0));
    // And back up as from a start once it sets again.
    level.touch(start + milliseconds(5000));
    CHECK(near(level.step(start + milliseconds(5250)), 0.25));
}

TEST(ease_in_starts_again_from_nothing_quicker) {
    OutputLevel level;
    const Clock::time_point start{};
    level.start(start);
    for (int ms = 10; ms <= 1200; ms += 10) {
        level.touch(start + milliseconds(ms));
        level.step(start + milliseconds(ms));
    }
    level.ease_in(start + milliseconds(1200), std::chrono::duration<double>(0.4));
    level.touch(start + milliseconds(1300));
    CHECK(near(level.step(start + milliseconds(1300)), 0.25));
    level.touch(start + milliseconds(1600));
    CHECK(near(level.step(start + milliseconds(1600)), 1.0));
    // A stall afterwards still ramps back as from a start, over a second.
    level.step(start + milliseconds(3000));
    level.touch(start + milliseconds(3500));
    CHECK(near(level.step(start + milliseconds(3500)), 0.5));
}
