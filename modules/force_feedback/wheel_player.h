#pragma once

// What the thread plays each tick, worked out from what the game set: the
// steady force shaped (force_shaping.h), the engine's tone and the road's
// texture added to it (vibration.h), the sum soft-clipped, and all of it
// scaled by the soft start, the watchdog and any ease-in (output_level.h).
// The damper and friction pass through, scaled the same way. Where the
// wheel's position can be read here, the steering's damping is worked out
// here too, from how fast it's turning now: the game sees the wheel a frame
// or two late, and damping pushed that late swung a geared wheel instead of
// calming it. No Godot and no wheel here, so the tests run it on a fake
// clock.

#include "force_shaping.h"
#include "output_level.h"
#include "vibration.h"

#include <array>
#include <cmath>
#include <optional>

// A spring on the steering, as DirectInput's spring condition has it: pushing back towards its
// centre past a deadband either side, each side with its own stiffness (a share of full scale per
// unit of the steering axis past the deadband; negative pushes away) and its own saturation (the
// most it pushes, 0 to 1). The sides are the steering axis's own: positive above the centre.
struct WheelSpring {
    double centre = 0.0;    // -1 to 1 of the steering axis
    double deadband = 0.0;  // either side of the centre, in the axis's units
    double positive_stiffness = 0.0;
    double negative_stiffness = 0.0;
    double positive_saturation = 1.0;
    double negative_saturation = 1.0;
};

struct WheelFrame {
    double force = 0.0;     // -1 to 1, positive turning the wheel left
    double damper = 0.0;    // 0 to 1 of the wheel's strongest
    double friction = 0.0;  // 0 to 1 of the wheel's strongest
};

class WheelPlayer {
public:
    using Clock = OutputLevel::Clock;

    // From nothing, as the wheel opens, to play `rate` ticks a second.
    void start(Clock::time_point now, double rate);
    // What the game sets. Each is a sign of life for the watchdog.
    void set_force(double force, Clock::time_point now);
    void set_tone(double amplitude, double frequency, Clock::time_point now);
    void set_texture(double amplitude, double frequency, Clock::time_point now);
    void set_damper(double damper, Clock::time_point now);
    void set_friction(double friction, Clock::time_point now);
    // The steering's damping: force (a share of full scale) per unit of the
    // steering axis (-1 to 1) a second, pushed against the way it's turning.
    // Signed: the game folds in which way the axis runs (negative where it
    // falls as the force's positive way turns it), the wheel's rotation and
    // its strength.
    void set_steering_damping(double damping, Clock::time_point now);
    // Damping on quick turning only (above about kSlowTau's pace), the same
    // way round: what calms a wheel swinging a few times a second without
    // weighing down the player's own, slower, steering.
    void set_quick_damping(double damping, Clock::time_point now);
    // The steering column locked (a key car's, switched off): free `play`
    // either side of `at` (the steering axis, -1 to 1), then pushed back at
    // `stiffness` (a share of full scale per unit of the axis past the play,
    // signed as the damping is), no more than full scale. NAN `at`: free.
    // Worked out here from the wheel's own fresh reading: a spring played
    // from the game's, a frame or two late, threw a geared wheel about.
    void set_column_lock(double at, double play, double stiffness, Clock::time_point now);
    // How stiffly the steady force changes as the wheel turns (a share of
    // full scale per unit of the steering axis, signed as the damping is),
    // and how long before now the game read the wheel it was worked out
    // for (s): the force is corrected here by how far the wheel has turned
    // since, read from this thread's own readings. The game's force reaches
    // the wheel some 40 ms after it moved; played that late, the tyres'
    // centring swung a geared wheel, and the damping that calmed it weighed
    // on every turn at speed. Corrected here, the spring is near enough
    // fresh. Call it with each set_force.
    void set_steering_spring(double stiffness, double lag, Clock::time_point now);
    // A spring on the steering (none until set), worked out here from the wheel's own fresh
    // reading, so it always pushes towards its centre: which way the axis runs against the force
    // comes from set_axis_direction, not the caller.
    void set_spring(const WheelSpring& spring, Clock::time_point now);
    void clear_spring(Clock::time_point now);
    // How the steering axis moves as a positive force turns the wheel: -1 where it falls (every
    // wheel Purr knows: the axis rises to the right, a positive force turns left), 1 where it rises.
    void set_axis_direction(int direction) { axis_falls_ = direction < 0 ? 1.0 : -1.0; }
    // Where the steering is now (-1 to 1), or nothing if it can't be read.
    void set_position(std::optional<double> position, Clock::time_point now);
    // Everything from nothing again over `seconds`: after a jump (a
    // teleport or a reset), whose first steps can spike.
    void ease_in(double seconds, Clock::time_point now);

    // One tick: what to play now.
    WheelFrame step(Clock::time_point now);
    // How long a tick is.
    Clock::duration period() const;
    // The steering as last read here (NAN if it can't be), and the damping's
    // push last tick: for the game's trace.
    double position() const { return position_ ? *position_ : NAN; }
    double damping() const { return damping_; }
    double spring() const { return spring_; }

private:
    // The longest a late tick counts as, so a stall doesn't leap.
    static constexpr double kLongestStep = 0.05;
    // How fast the steering turns is smoothed over this (s): reports come a
    // few hundred times a second, a step at a time.
    static constexpr double kTurningTau = 0.006;
    // Turning slower than this pace (s) isn't quick: a hand's steering.
    static constexpr double kSlowTau = 0.16;
    // The most the damping may ask, as a share of full scale.
    static constexpr double kMostDamping = 1.0;
    // The most the damping on quick turning may ask of it: enough to calm a
    // wheel let go swinging; with the whole push, a hand-over-hand turn into
    // a tight corner fought the wheel's full strength.
    static constexpr double kMostQuick = 0.1;
    // The most the column's lock may push, as a share of full scale.
    static constexpr double kMostLock = 1.0;
    // The most the spring's correction may push, as a share of full scale:
    // a fault in its reference never throws the wheel.
    static constexpr double kMostSpring = 0.5;
    // The readings kept to look back on (each tick's), and the furthest back (s).
    static constexpr std::size_t kHistory = 128;
    static constexpr double kLongestLag = 0.15;

    double rate_ = 400.0;
    double force_ = 0.0;
    double damper_ = 0.0;
    double friction_ = 0.0;
    double steering_damping_ = 0.0;
    double quick_damping_ = 0.0;
    // The column lock is a symmetric spring whose direction comes with its stiffness's sign.
    std::optional<WheelSpring> lock_;
    double lock_falls_ = 1.0;
    std::optional<WheelSpring> spring_setting_;
    double axis_falls_ = 1.0;  // 1 where a positive force moves the axis down
    double turning_slow_ = 0.0;  // the turning, smoothed over kSlowTau
    std::optional<double> position_;
    std::optional<double> last_position_;
    double turning_ = 0.0;  // axis units a second, smoothed
    double damping_ = 0.0;  // its push last tick
    double spring_stiffness_ = 0.0;
    std::optional<double> spring_at_;  // where the wheel was when the game read it
    double spring_ = 0.0;  // its push last tick
    std::array<std::pair<Clock::time_point, double>, kHistory> history_{};
    std::size_t history_next_ = 0;
    std::size_t history_count_ = 0;
    // Where the wheel was `seconds` ago by this thread's readings, or nothing.
    std::optional<double> position_ago(double seconds, Clock::time_point now) const;
    ForceShaping shaping_;
    Tone tone_;
    Texture texture_;
    OutputLevel level_;
    Clock::time_point last_step_{};
};
