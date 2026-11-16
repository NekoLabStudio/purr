#include "steering_wheel.h"

#include "core/object/class_db.h"
#include "servers/display/display_server.h"

#include <algorithm>

namespace {

Dictionary describe(const std::optional<FoundWheel>& wheel) {
    Dictionary found;
    if (wheel) {
        found["name"] = String::utf8(wheel->name.c_str());
        found["new"] = !wheel->verified;
        found["vendor_id"] = wheel->vendor;
        found["product_id"] = wheel->product;
        found["family"] = String::utf8(wheel->family.c_str());
    }
    return found;
}

std::uint16_t usb(int64_t id) {
    return static_cast<std::uint16_t>(std::clamp<int64_t>(id, 0, 0xFFFF));
}

}  // namespace

// The game's own window, which Windows' force feedback asks for.
static std::uintptr_t native_window() {
    DisplayServer* display = DisplayServer::get_singleton();
    return display ? static_cast<std::uintptr_t>(display->window_get_native_handle(DisplayServerEnums::WINDOW_HANDLE)) : 0;
}

void SteeringWheel::_bind_methods() {
    ClassDB::bind_method(D_METHOD("find_wheel", "vendor_id", "product_id"), &SteeringWheel::find_wheel,
                         DEFVAL(0), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("open", "vendor_id", "product_id"), &SteeringWheel::open, DEFVAL(0), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("close"), &SteeringWheel::close);
    ClassDB::bind_method(D_METHOD("is_open"), &SteeringWheel::is_open);
    ClassDB::bind_method(D_METHOD("get_device_name"), &SteeringWheel::get_device_name);
    ClassDB::bind_method(D_METHOD("set_force", "force"), &SteeringWheel::set_force);
    ClassDB::bind_method(D_METHOD("get_force"), &SteeringWheel::get_force);
    ClassDB::bind_method(D_METHOD("set_vibration", "amplitude", "frequency"), &SteeringWheel::set_vibration);
    ClassDB::bind_method(D_METHOD("set_texture", "amplitude", "frequency"), &SteeringWheel::set_texture);
    ClassDB::bind_method(D_METHOD("set_damper", "damper"), &SteeringWheel::set_damper);
    ClassDB::bind_method(D_METHOD("get_damper"), &SteeringWheel::get_damper);
    ClassDB::bind_method(D_METHOD("set_friction", "friction"), &SteeringWheel::set_friction);
    ClassDB::bind_method(D_METHOD("get_friction"), &SteeringWheel::get_friction);
    ClassDB::bind_method(D_METHOD("set_steering_damping", "damping"), &SteeringWheel::set_steering_damping);
    ClassDB::bind_method(D_METHOD("get_steering_damping"), &SteeringWheel::get_steering_damping);
    ClassDB::bind_method(D_METHOD("set_quick_damping", "damping"), &SteeringWheel::set_quick_damping);
    ClassDB::bind_method(D_METHOD("get_quick_damping"), &SteeringWheel::get_quick_damping);
    ClassDB::bind_method(D_METHOD("set_column_lock", "at", "play", "stiffness"), &SteeringWheel::set_column_lock);
    ClassDB::bind_method(D_METHOD("set_steering_spring", "stiffness", "lag"), &SteeringWheel::set_steering_spring);
    ClassDB::bind_method(D_METHOD("set_spring", "centre", "deadband", "positive_stiffness", "negative_stiffness",
                                  "positive_saturation", "negative_saturation"),
                         &SteeringWheel::set_spring, DEFVAL(1.0), DEFVAL(1.0));
    ClassDB::bind_method(D_METHOD("clear_spring"), &SteeringWheel::clear_spring);
    ClassDB::bind_method(D_METHOD("set_rev_lights", "share"), &SteeringWheel::set_rev_lights);
    ClassDB::bind_method(D_METHOD("get_rev_lights"), &SteeringWheel::get_rev_lights);
    ClassDB::bind_method(D_METHOD("reads_position"), &SteeringWheel::reads_position);
    ClassDB::bind_method(D_METHOD("get_position"), &SteeringWheel::get_position);
    ClassDB::bind_method(D_METHOD("get_spring_force"), &SteeringWheel::get_spring_force);
    ClassDB::bind_method(D_METHOD("get_damping_force"), &SteeringWheel::get_damping_force);
    ClassDB::bind_method(D_METHOD("set_rotation", "degrees"), &SteeringWheel::set_rotation);
    ClassDB::bind_method(D_METHOD("get_rotation"), &SteeringWheel::get_rotation);
    ClassDB::bind_method(D_METHOD("ease_in", "seconds"), &SteeringWheel::ease_in);
    ClassDB::bind_method(D_METHOD("prepare"), &SteeringWheel::prepare);

    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "force", PROPERTY_HINT_RANGE, "-1,1,0.001"), "set_force", "get_force");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "damper", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_damper", "get_damper");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "friction", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_friction", "get_friction");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "steering_damping"), "set_steering_damping", "get_steering_damping");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "quick_damping"), "set_quick_damping", "get_quick_damping");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rev_lights", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_rev_lights", "get_rev_lights");
}

Dictionary SteeringWheel::find_wheel(int64_t vendor_id, int64_t product_id) {
    return describe(session_.find_wheel(usb(vendor_id), usb(product_id)));
}

bool SteeringWheel::open(int64_t vendor_id, int64_t product_id) {
    return session_.open(native_window(), usb(vendor_id), usb(product_id));
}

void SteeringWheel::close() {
    session_.close();
}

bool SteeringWheel::is_open() const {
    return session_.is_open();
}

String SteeringWheel::get_device_name() const {
    const std::optional<FoundWheel> wheel = session_.wheel();
    return wheel ? String::utf8(wheel->name.c_str()) : String();
}

void SteeringWheel::set_force(double force) {
    force_ = force;
    session_.set_force(force);
}

void SteeringWheel::set_vibration(double amplitude, double frequency) {
    session_.set_vibration(amplitude, frequency);
}

void SteeringWheel::set_texture(double amplitude, double frequency) {
    session_.set_texture(amplitude, frequency);
}

void SteeringWheel::set_damper(double damper) {
    damper_ = damper;
    session_.set_damper(damper);
}

void SteeringWheel::set_friction(double friction) {
    friction_ = friction;
    session_.set_friction(friction);
}

void SteeringWheel::set_steering_damping(double damping) {
    steering_damping_ = damping;
    session_.set_steering_damping(damping);
}

void SteeringWheel::set_quick_damping(double damping) {
    quick_damping_ = damping;
    session_.set_quick_damping(damping);
}

void SteeringWheel::set_column_lock(double at, double play, double stiffness) {
    session_.set_column_lock(at, play, stiffness);
}

void SteeringWheel::set_steering_spring(double stiffness, double lag) {
    session_.set_steering_spring(stiffness, lag);
}

void SteeringWheel::set_spring(double centre, double deadband, double positive_stiffness, double negative_stiffness,
                                    double positive_saturation, double negative_saturation) {
    session_.set_spring(
        WheelSpring{centre, deadband, positive_stiffness, negative_stiffness, positive_saturation, negative_saturation});
}

void SteeringWheel::clear_spring() {
    session_.clear_spring();
}

void SteeringWheel::set_rev_lights(double share) {
    rev_lights_ = share;
    session_.set_leds(WheelSession::rev_lights(share));
}

double SteeringWheel::get_spring_force() {
    return session_.thread_spring();
}

bool SteeringWheel::reads_position() const {
    return session_.reads_position();
}

double SteeringWheel::get_position() {
    return session_.thread_position();
}

double SteeringWheel::get_damping_force() {
    return session_.thread_damping();
}

int64_t SteeringWheel::set_rotation(int64_t degrees) {
    return session_.set_rotation(static_cast<int>(std::clamp<int64_t>(degrees, 0, 10000)));
}

int64_t SteeringWheel::get_rotation() {
    return session_.rotation();
}

void SteeringWheel::ease_in(double seconds) {
    session_.ease_in(seconds);
}

void SteeringWheel::prepare() {
    session_.prepare();
}
