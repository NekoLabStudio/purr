#pragma once

// One binding, as saved: a readable Dictionary.
//   {"key": "W"}                                          a key, by where it is (Godot's name for it)
//   {"mouse": "left"}                                     a mouse button or the wheel
//   {"device": "gamepad", "button": "a"}                  a joypad button, by name or number
//   {"device": "...", "axis": "left_x", "from": 0, "to": 1, "deadzone": 0.05, "curve": 1.5}
//   {"device": "...", "hat": "up"}                        a steering wheel's hat
// "device" is a joypad's name (with its "guid" too where known), or "gamepad" for any
// controller. An axis maps raw values from "from" (0) to "to" (1), which covers its range
// and inversion; "deadzone" ignores the start of the travel and "curve" above 1 makes it
// progressive. These are the pure parts: names, calibration, comparing and describing.

#include "core/input/input_enums.h"
#include "core/os/keyboard.h"
#include "core/string/ustring.h"
#include "core/variant/dictionary.h"

namespace InputBinding {

// Godot's D-pad buttons, which a joystick's hat comes in as (and a wheel's own buttons share).
constexpr int HAT_FIRST_BUTTON = 11;
// A wheel's hat directions moved to buttons of their own, below JOY_BUTTON_MAX and beyond any
// wheel's own buttons, so they're told apart from the wheel's buttons with the same numbers.
constexpr int HAT_BUTTON_UP = 124;
constexpr int HAT_BUTTON_RIGHT = 125;
constexpr int HAT_BUTTON_DOWN = 126;
constexpr int HAT_BUTTON_LEFT = 127;
// What a wheel's renumbered press becomes when its own buttons are read instead: bound to nothing.
constexpr int DROPPED_BUTTON = 123;

// A joypad button's or axis's index from its name ("a", "left_x") or number; -1 if neither.
int button_index(const Variant &p_button);
int axis_index(const Variant &p_axis);
// The name of a joypad button or axis Godot names, else its number.
Variant button_name(int p_index);
Variant axis_name(int p_index);
// A hat direction ("up") as its own button, and back; -1 / empty for anything else.
int hat_button(const String &p_direction);
String hat_direction(int p_button);
// The bit a direction has in a hat's state (up 1, right 2, down 4, left 8).
int hat_mask(const String &p_direction);
// Godot's D-pad button for a hat direction (as Godot reports a hat), and back.
int dpad_button(const String &p_direction);
String dpad_direction(int p_button);

// A mouse button from its name ("left", "wheel_up"), and back.
MouseButton mouse_button(const String &p_name);
String mouse_name(MouseButton p_button);
// A key's code by its name, as Godot writes it ("W", "Space"), looked up once.
Key keycode(const String &p_name);

// An axis's raw value through the binding's calibration: 0 to 1.
float calibrate(const Dictionary &p_binding, float p_raw);
// Whether an axis binding rises (to above from).
bool rises(const Dictionary &p_binding);

// Whether two bindings are the same input on the same device.
bool same_input(const Dictionary &p_a, const Dictionary &p_b);
// Whether a binding is on a joypad (not the keyboard or mouse).
bool is_joypad(const Dictionary &p_binding);

// The prompt's control for a binding (see prompt_images.py), on a gamepad family: "a",
// "left_stick_right", "dpad_up"; for a key "keyboard" family's key name; empty if it has none.
String prompt_control(const Dictionary &p_binding);
// A key's name for prompts: Godot's name, lower case, spaces as underscores ("kp_enter").
String key_prompt_name(Key p_keycode);

// A binding in words, for a player: "W", "Left mouse button", "Button 7", "Left stick right".
// p_key_label is what the key says on this keyboard's layout, when known.
String describe(const Dictionary &p_binding, const String &p_key_label = String());

} // namespace InputBinding
