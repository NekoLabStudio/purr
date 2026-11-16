#include "wheel_session.h"

#include "wheel_log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

#ifdef __APPLE__
#include <pthread/qos.h>
#endif

namespace {

using Clock = WheelPlayer::Clock;

// The rates are measured over this long after the wheel opens, then logged once.
constexpr auto kMeasureFor = std::chrono::seconds(10);

}  // namespace

WheelSession::WheelSession() : WheelSession(WheelOutput::create()) {}

WheelSession::WheelSession(std::unique_ptr<WheelOutput> output) : output_(std::move(output)) {}

WheelSession::~WheelSession() {
    close();
}

std::optional<FoundWheel> WheelSession::find_wheel(std::uint16_t vendor, std::uint16_t product) {
    std::lock_guard<std::mutex> wheel(output_mutex_);
    return playing_ ? output_->wheel() : output_->find(vendor, product);
}

bool WheelSession::open(std::uintptr_t window, std::uint16_t vendor, std::uint16_t product) {
    close();
    int rate = 0;
    int direction = -1;
    {
        std::lock_guard<std::mutex> wheel(output_mutex_);
        if (!output_->open(window, vendor, product)) {
            return false;
        }
        rate = std::max(output_->update_hz(), 1);
        direction = output_->axis_direction();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        player_.start(Clock::now(), rate);
        player_.set_axis_direction(direction);
        sent_leds_.reset();
        playing_ = true;
    }
    answering_ = true;
    thread_ = std::thread(&WheelSession::play, this);
    return true;
}

void WheelSession::close() {
    if (!thread_.joinable()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        playing_ = false;
    }
    wake_.notify_all();
    thread_.join();
    std::lock_guard<std::mutex> wheel(output_mutex_);
    output_->close();
    answering_ = false;
}

bool WheelSession::is_open() const {
    return playing_ && answering_;
}

std::optional<FoundWheel> WheelSession::wheel() const {
    std::lock_guard<std::mutex> wheel(output_mutex_);
    return output_->wheel();
}

void WheelSession::set_force(double force) {
    std::lock_guard<std::mutex> lock(mutex_);
    player_.set_force(force, Clock::now());
}

void WheelSession::set_vibration(double amplitude, double frequency) {
    std::lock_guard<std::mutex> lock(mutex_);
    player_.set_tone(amplitude, frequency, Clock::now());
}

void WheelSession::set_texture(double amplitude, double frequency) {
    std::lock_guard<std::mutex> lock(mutex_);
    player_.set_texture(amplitude, frequency, Clock::now());
}

void WheelSession::set_damper(double damper) {
    std::lock_guard<std::mutex> lock(mutex_);
    player_.set_damper(damper, Clock::now());
}

void WheelSession::set_friction(double friction) {
    std::lock_guard<std::mutex> lock(mutex_);
    player_.set_friction(friction, Clock::now());
}

void WheelSession::set_steering_damping(double damping) {
    std::lock_guard<std::mutex> lock(mutex_);
    player_.set_steering_damping(damping, Clock::now());
}

void WheelSession::set_quick_damping(double damping) {
    std::lock_guard<std::mutex> lock(mutex_);
    player_.set_quick_damping(damping, Clock::now());
}

void WheelSession::set_column_lock(double at, double play, double stiffness) {
    std::lock_guard<std::mutex> lock(mutex_);
    player_.set_column_lock(at, play, stiffness, Clock::now());
}

void WheelSession::set_steering_spring(double stiffness, double lag) {
    std::lock_guard<std::mutex> lock(mutex_);
    player_.set_steering_spring(stiffness, lag, Clock::now());
}

void WheelSession::set_spring(const WheelSpring& spring) {
    std::lock_guard<std::mutex> lock(mutex_);
    player_.set_spring(spring, Clock::now());
}

void WheelSession::clear_spring() {
    std::lock_guard<std::mutex> lock(mutex_);
    player_.clear_spring(Clock::now());
}

void WheelSession::set_leds(std::uint32_t leds) {
    std::lock_guard<std::mutex> lock(mutex_);
    leds_ = leds;
}

std::uint32_t WheelSession::rev_lights(double share, int count) {
    count = std::clamp(count, 0, 31);
    const long lit = std::isfinite(share) ? std::lround(std::clamp(share, 0.0, 1.0) * count) : 0;
    return (1u << lit) - 1u;
}

void WheelSession::ease_in(double seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    player_.ease_in(seconds, Clock::now());
}

bool WheelSession::reads_position() const {
    return is_open() && reads_position_;
}

double WheelSession::thread_position() {
    std::lock_guard<std::mutex> lock(mutex_);
    return player_.position();
}

double WheelSession::thread_damping() {
    std::lock_guard<std::mutex> lock(mutex_);
    return player_.damping();
}

double WheelSession::thread_spring() {
    std::lock_guard<std::mutex> lock(mutex_);
    return player_.spring();
}

int WheelSession::set_rotation(int degrees) {
    std::lock_guard<std::mutex> wheel(output_mutex_);
    if (!playing_) {
        return 0;
    }
    return output_->set_rotation(std::clamp(degrees, 0, 10000));
}

int WheelSession::rotation() {
    std::lock_guard<std::mutex> wheel(output_mutex_);
    return playing_ ? output_->rotation() : 0;
}

void WheelSession::prepare() {
    std::lock_guard<std::mutex> wheel(output_mutex_);
    output_->prepare();
}

void WheelSession::report_rates(std::uint64_t ticks, double seconds) {
    OutputCounts counts;
    std::string name;
    {
        std::lock_guard<std::mutex> wheel(output_mutex_);
        counts = output_->counts();
        const std::optional<FoundWheel> open = output_->wheel();
        name = open ? open->name : "the wheel";
    }
    wheel_log(name + ": " + std::to_string(std::lround(ticks / seconds)) + " updates a second, " +
              std::to_string(std::lround(counts.reports / seconds)) + " reports a second, " +
              std::to_string(counts.errors) + " answered with errors");
}

// The thread: what the player works out each tick, played to the wheel at the rate it keeps up
// with, until closed or the wheel stops answering. The driver sends only what changed, in its own
// steps; the rev lights go after the forces, only as they change.
void WheelSession::play() {
#ifdef __APPLE__
    // Ahead of the caller's own threads: a late tick is felt.
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    const Clock::time_point opened = Clock::now();
    std::uint64_t ticks = 0;
    bool reported = false;
    auto next = opened;
    std::unique_lock<std::mutex> lock(mutex_);
    while (playing_) {
        lock.unlock();
        std::optional<double> position;
        {
            std::lock_guard<std::mutex> wheel(output_mutex_);
            position = output_->position();
        }
        reads_position_ = position.has_value();
        lock.lock();
        const Clock::time_point now = Clock::now();
        player_.set_position(position, now);
        const WheelFrame frame = player_.step(now);
        const Clock::duration period = player_.period();
        const std::optional<std::uint32_t> leds = leds_ != sent_leds_ ? leds_ : std::nullopt;
        lock.unlock();
        bool ok;
        {
            std::lock_guard<std::mutex> wheel(output_mutex_);
            ok = output_->play(frame.force, frame.damper, frame.friction);
            if (ok && leds) {
                ok = output_->set_leds(*leds);
            }
        }
        ++ticks;
        if (!reported && now - opened >= kMeasureFor) {
            report_rates(ticks, std::chrono::duration<double>(now - opened).count());
            reported = true;
        }
        lock.lock();
        if (leds && ok) {
            sent_leds_ = leds;
        }
        if (!ok) {
            answering_ = false;  // unplugged: the caller opens it again when it's back
            break;
        }
        // Behind (a stall, a slow write): carry on from now, never in a burst.
        next = std::max(next + period, now);
        wake_.wait_until(lock, next, [this] { return !playing_; });
    }
}
