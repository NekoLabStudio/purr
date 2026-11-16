#pragma once

// A force feedback wheel, played on a thread of its own: what the caller sets (a steady force,
// the engine's tone, the road's texture, a damper, friction, springs, the rev lights) as often as
// it likes, and the thread plays it to the wheel as often as that wheel keeps up (its driver says),
// reading the wheel's position each tick, so the wheel's feel doesn't hang on the caller's frame
// rate and the caller never waits on the wheel (wheel_player.h). It eases in as the wheel opens
// and fades out if the caller stops setting (output_level.h). Plain C++ (no Godot, exceptions or
// RTTI): SteeringWheel is the game's way to it, and other programs drive it directly.

#include "wheel_output.h"
#include "wheel_player.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

class WheelSession {
public:
    // This platform's wheels (WheelOutput::create), or another output (the tests' fake).
    WheelSession();
    explicit WheelSession(std::unique_ptr<WheelOutput> output);
    ~WheelSession();
    WheelSession(const WheelSession&) = delete;
    WheelSession& operator=(const WheelSession&) = delete;

    // The wheel this would open (the one with these USB IDs when they aren't 0), or the open one.
    std::optional<FoundWheel> find_wheel(std::uint16_t vendor = 0, std::uint16_t product = 0);
    // Takes that wheel for force feedback and starts the thread. `window` is the native window
    // handle Windows needs to take a wheel; 0 elsewhere.
    bool open(std::uintptr_t window = 0, std::uint16_t vendor = 0, std::uint16_t product = 0);
    void close();
    // Open, and the wheel still answering (false once it's unplugged: open it again when it's back).
    bool is_open() const;
    std::optional<FoundWheel> wheel() const;

    // -1 to 1, positive turning the wheel to the left; past 1 it's rounded off short of the
    // wheel's strongest (soft clipping).
    void set_force(double force);
    // The engine's tone: a sine, its amplitude (0 to 1) and frequency (Hz).
    void set_vibration(double amplitude, double frequency);
    // The road's texture: a band of noise round a frequency (Hz), its RMS amplitude (0 to 1).
    void set_texture(double amplitude, double frequency);
    // 0 to 1 of the wheel's strongest damper, and of its friction.
    void set_damper(double damper);
    void set_friction(double friction);
    // WheelPlayer's steering damping, quick damping, column lock and steering spring.
    void set_steering_damping(double damping);
    void set_quick_damping(double damping);
    void set_column_lock(double at, double play, double stiffness);
    void set_steering_spring(double stiffness, double lag);
    // A spring towards a centre on the steering (WheelSpring), worked out on the thread from the
    // wheel's own position; only where reads_position().
    void set_spring(const WheelSpring& spring);
    void clear_spring();
    // The rev lights, one bit each from the first to light (bit 0); sent on the thread, only as
    // they change. Nothing on a wheel without them.
    void set_leds(std::uint32_t leds);
    // The lights for a share (0 to 1) of `count` lights, lit from the first.
    static std::uint32_t rev_lights(double share, int count = 5);
    // Everything from nothing again over `seconds`: after a teleport, a reset or a pause, whose
    // first steps can spike.
    void ease_in(double seconds);

    // Whether the open wheel's position can be read on the thread.
    bool reads_position() const;
    // The steering axis as the thread last read it (NAN if it can't), and the pushes its damping
    // and its steering spring added: for a trace.
    double thread_position();
    double thread_damping();
    double thread_spring();
    // The wheel's rotation, lock to lock in degrees: set as near as the wheel goes, returning
    // what it's set to, or 0 where it can't be set from here. What it was goes back on close.
    int set_rotation(int degrees);
    // As last set or read from the wheel; 0 if unknown.
    int rotation();
    // Switches any wheel attached in a starting mode to its own, without taking it.
    void prepare();

private:
    void play();
    // The measured rates, once, a while after the wheel opens.
    void report_rates(std::uint64_t ticks, double seconds);

    std::unique_ptr<WheelOutput> output_;
    // The wheel: one caller at a time (the thread, or the caller setting its rotation).
    mutable std::mutex output_mutex_;
    std::thread thread_;
    // What the caller sets, and the thread's state.
    std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<bool> playing_{false};
    std::optional<std::uint32_t> leds_;  // to send, when it differs from what was
    std::optional<std::uint32_t> sent_leds_;
    std::atomic<bool> answering_{false};
    std::atomic<bool> reads_position_{false};  // the thread read the wheel's position last tick
    WheelPlayer player_;
};
