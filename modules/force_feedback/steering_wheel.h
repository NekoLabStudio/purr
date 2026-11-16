#pragma once

// SteeringWheel: the game's way to a force feedback steering wheel
// (scripts/force_feedback.gd works out what to play): Godot's face on a
// WheelSession (wheel_session.h), which plays what the game sets on a thread
// of its own.

#include "wheel_session.h"

#include "core/object/ref_counted.h"
#include "core/variant/dictionary.h"

class SteeringWheel : public RefCounted {
    GDCLASS(SteeringWheel, RefCounted)

public:
    // {"name": its name, "new": true if its force feedback isn't yet
    // confirmed on the real thing, "vendor_id" and "product_id": its USB
    // IDs, "family": its driver's ("lg4ff", "hidpp", "t150", "t300",
    // "fanatec", "pid", "simagic", "logitech_stream", or "directinput" on
    // Windows)}, or empty when there's no wheel. Given the IDs of the wheel
    // bound to the steering, that one when it's there.
    Dictionary find_wheel(int64_t vendor_id = 0, int64_t product_id = 0);
    // Takes that wheel for force feedback.
    bool open(int64_t vendor_id = 0, int64_t product_id = 0);
    void close();
    bool is_open() const;
    String get_device_name() const;
    void set_force(double force);
    double get_force() const { return force_; }
    void set_vibration(double amplitude, double frequency);
    void set_texture(double amplitude, double frequency);
    void set_damper(double damper);
    double get_damper() const { return damper_; }
    void set_friction(double friction);
    double get_friction() const { return friction_; }
    void set_steering_damping(double damping);
    double get_steering_damping() const { return steering_damping_; }
    void set_quick_damping(double damping);
    double get_quick_damping() const { return quick_damping_; }
    void set_column_lock(double at, double play, double stiffness);
    void set_steering_spring(double stiffness, double lag);
    void set_spring(double centre, double deadband, double positive_stiffness, double negative_stiffness,
                    double positive_saturation = 1.0, double negative_saturation = 1.0);
    void clear_spring();
    void set_rev_lights(double share);
    double get_rev_lights() const { return rev_lights_; }
    bool reads_position() const;
    double get_position();
    double get_spring_force();
    double get_damping_force();
    int64_t set_rotation(int64_t degrees);
    int64_t get_rotation();
    void ease_in(double seconds);
    void prepare();

protected:
    static void _bind_methods();

private:
    WheelSession session_;
    // As last set, for the properties.
    double force_ = 0.0;
    double damper_ = 0.0;
    double friction_ = 0.0;
    double steering_damping_ = 0.0;
    double quick_damping_ = 0.0;
    double rev_lights_ = 0.0;
};
