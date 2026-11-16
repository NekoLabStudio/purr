// The wheel session's thread (wheel_session.h) against a fake wheel, in real time.

#include "test.h"
#include "wheel_session.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

namespace {

// A wheel that records what it's played, held only through the session.
struct FakeState {
    std::atomic<int> plays{0};
    std::atomic<int> led_sends{0};
    std::atomic<std::uint32_t> leds{0};
    std::atomic<bool> answering{true};
    std::atomic<bool> closed{false};
};

class FakeOutput : public WheelOutput {
public:
    explicit FakeOutput(FakeState& state) : state_(state) {}
    std::optional<FoundWheel> find(std::uint16_t, std::uint16_t) override { return FoundWheel{"Fake wheel"}; }
    bool open(std::uintptr_t, std::uint16_t, std::uint16_t) override { return true; }
    void close() override { state_.closed = true; }
    std::optional<FoundWheel> wheel() const override { return FoundWheel{"Fake wheel"}; }
    bool set_force(double) override { return state_.answering; }
    bool set_damper(double) override { return state_.answering; }
    bool set_friction(double) override { return state_.answering; }
    bool play(double, double, double) override {
        ++state_.plays;
        return state_.answering;
    }
    bool set_leds(std::uint32_t leds) override {
        ++state_.led_sends;
        state_.leds = leds;
        return state_.answering;
    }
    std::optional<double> position() override { return 0.0; }
    int update_hz() const override { return 1000; }
    void stop() override {}

private:
    FakeState& state_;
};

void wait_for(const std::function<bool()>& done) {
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!done() && std::chrono::steady_clock::now() < give_up) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

}  // namespace

TEST(session_plays_the_wheel_on_its_own_thread) {
    FakeState state;
    WheelSession session(std::make_unique<FakeOutput>(state));
    CHECK(session.find_wheel()->name == "Fake wheel");
    CHECK(session.open());
    CHECK(session.is_open());
    wait_for([&] { return state.plays > 20; });
    CHECK(state.plays > 20);
    CHECK(session.reads_position());
    session.close();
    CHECK(!session.is_open());
    CHECK(state.closed);
}

TEST(session_sends_the_rev_lights_once_as_they_change) {
    FakeState state;
    WheelSession session(std::make_unique<FakeOutput>(state));
    session.open();
    session.set_leds(0x05);
    wait_for([&] { return state.led_sends > 0; });
    const int plays = state.plays;
    wait_for([&] { return state.plays > plays + 20; });
    CHECK_EQ(int(state.led_sends), 1);
    CHECK_EQ(int(state.leds), 0x05);
    session.set_leds(0x07);
    wait_for([&] { return state.led_sends > 1; });
    CHECK_EQ(int(state.leds), 0x07);
}

TEST(session_stops_once_the_wheel_stops_answering) {
    FakeState state;
    WheelSession session(std::make_unique<FakeOutput>(state));
    session.open();
    state.answering = false;
    wait_for([&] { return !session.is_open(); });
    CHECK(!session.is_open());
}

TEST(session_rev_lights_light_from_the_first) {
    CHECK_EQ(int(WheelSession::rev_lights(0.0)), 0x00);
    CHECK_EQ(int(WheelSession::rev_lights(0.4)), 0x03);
    CHECK_EQ(int(WheelSession::rev_lights(1.0)), 0x1F);
    CHECK_EQ(int(WheelSession::rev_lights(2.0)), 0x1F);
    CHECK_EQ(int(WheelSession::rev_lights(NAN)), 0x00);
}
