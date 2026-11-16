#include "register_types.h"

#include "steering_wheel.h"
#include "steering_wheel_hats.h"
#include "wheel_log.h"
#include "wheel_output.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/string/print_string.h"

// On macOS, SDL hands game controllers to Apple's Game Controller framework, which doesn't pass
// steering wheels on. With input_devices/steering_wheels on, SDL reads every controller through its
// own drivers instead, wheels included. Set before SDL starts (with the servers, after this).
static void read_steering_wheels() {
    const bool wheels = GLOBAL_DEF_BASIC(PropertyInfo(Variant::BOOL, "input_devices/steering_wheels"), false);
#ifdef MACOS_ENABLED
    if (wheels && !OS::get_singleton()->has_environment("SDL_JOYSTICK_MFI")) {
        OS::get_singleton()->set_environment("SDL_JOYSTICK_MFI", "0");
    }
#else
    (void)wheels;
#endif
}

void initialize_force_feedback_module(ModuleInitializationLevel p_level) {
    if (p_level == MODULE_INITIALIZATION_LEVEL_CORE) {
        read_steering_wheels();
        return;
    }
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    GDREGISTER_CLASS(SteeringWheel);
    GDREGISTER_CLASS(SteeringWheelHats);
    set_wheel_log([](const std::string& line) { print_line(String::utf8(line.c_str())); });
    WheelOutput::watch();
}

void uninitialize_force_feedback_module(ModuleInitializationLevel p_level) {
    if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    set_wheel_log(nullptr);
}
