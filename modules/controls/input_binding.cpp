#include "input_binding.h"

#include "core/os/keyboard.h"
#include "core/string/translation_server.h"
#include "core/templates/hash_map.h"

namespace {

// Godot's joypad buttons by name, in its order (JoyButton).
const char *BUTTONS[] = { "a", "b", "x", "y", "back", "guide", "start", "left_stick", "right_stick", "left_shoulder",
	"right_shoulder", "dpad_up", "dpad_down", "dpad_left", "dpad_right", "misc1", "paddle1", "paddle2", "paddle3",
	"paddle4", "touchpad" };
const int BUTTON_COUNT = sizeof(BUTTONS) / sizeof(BUTTONS[0]);
const char *AXES[] = { "left_x", "left_y", "right_x", "right_y", "trigger_left", "trigger_right" };
const int AXIS_COUNT = sizeof(AXES) / sizeof(AXES[0]);

struct MouseName {
	MouseButton button;
	const char *name;
};
const MouseName MOUSE[] = { { MouseButton::LEFT, "left" }, { MouseButton::RIGHT, "right" }, { MouseButton::MIDDLE, "middle" },
	{ MouseButton::WHEEL_UP, "wheel_up" }, { MouseButton::WHEEL_DOWN, "wheel_down" }, { MouseButton::WHEEL_LEFT, "wheel_left" },
	{ MouseButton::WHEEL_RIGHT, "wheel_right" }, { MouseButton::MB_XBUTTON1, "back" }, { MouseButton::MB_XBUTTON2, "forward" } };

String tr(const char *p_text) {
	TranslationServer *translations = TranslationServer::get_singleton();
	return translations ? String(translations->translate(p_text)) : String(p_text);
}

String words(const String &p_name) {
	return p_name.replace("_", " ").capitalize();
}

} // namespace

namespace InputBinding {

int button_index(const Variant &p_button) {
	if (p_button.get_type() == Variant::INT || p_button.get_type() == Variant::FLOAT) {
		return int(p_button);
	}
	const String name = p_button;
	for (int i = 0; i < BUTTON_COUNT; i++) {
		if (name == BUTTONS[i]) {
			return i;
		}
	}
	return name.is_valid_int() ? name.to_int() : -1;
}

int axis_index(const Variant &p_axis) {
	if (p_axis.get_type() == Variant::INT || p_axis.get_type() == Variant::FLOAT) {
		return int(p_axis);
	}
	const String name = p_axis;
	for (int i = 0; i < AXIS_COUNT; i++) {
		if (name == AXES[i]) {
			return i;
		}
	}
	return name.is_valid_int() ? name.to_int() : -1;
}

Variant button_name(int p_index) {
	return p_index >= 0 && p_index < BUTTON_COUNT ? Variant(String(BUTTONS[p_index])) : Variant(p_index);
}

Variant axis_name(int p_index) {
	return p_index >= 0 && p_index < AXIS_COUNT ? Variant(String(AXES[p_index])) : Variant(p_index);
}

int hat_button(const String &p_direction) {
	if (p_direction == "up") {
		return HAT_BUTTON_UP;
	} else if (p_direction == "right") {
		return HAT_BUTTON_RIGHT;
	} else if (p_direction == "down") {
		return HAT_BUTTON_DOWN;
	} else if (p_direction == "left") {
		return HAT_BUTTON_LEFT;
	}
	return -1;
}

String hat_direction(int p_button) {
	switch (p_button) {
		case HAT_BUTTON_UP:
			return "up";
		case HAT_BUTTON_RIGHT:
			return "right";
		case HAT_BUTTON_DOWN:
			return "down";
		case HAT_BUTTON_LEFT:
			return "left";
	}
	return String();
}

int hat_mask(const String &p_direction) {
	return p_direction == "up" ? 1 : p_direction == "right" ? 2 : p_direction == "down" ? 4 : p_direction == "left" ? 8 : 0;
}

int dpad_button(const String &p_direction) {
	return p_direction == "up" ? int(JoyButton::DPAD_UP) : p_direction == "down" ? int(JoyButton::DPAD_DOWN) : p_direction == "left" ? int(JoyButton::DPAD_LEFT) : p_direction == "right" ? int(JoyButton::DPAD_RIGHT) : -1;
}

String dpad_direction(int p_button) {
	switch (JoyButton(p_button)) {
		case JoyButton::DPAD_UP:
			return "up";
		case JoyButton::DPAD_DOWN:
			return "down";
		case JoyButton::DPAD_LEFT:
			return "left";
		case JoyButton::DPAD_RIGHT:
			return "right";
		default:
			return String();
	}
}

MouseButton mouse_button(const String &p_name) {
	for (const MouseName &each : MOUSE) {
		if (p_name == each.name) {
			return each.button;
		}
	}
	return MouseButton::NONE;
}

String mouse_name(MouseButton p_button) {
	for (const MouseName &each : MOUSE) {
		if (p_button == each.button) {
			return each.name;
		}
	}
	return String();
}

Key keycode(const String &p_name) {
	static HashMap<String, Key> known;
	const Key *found = known.getptr(p_name);
	if (found) {
		return *found;
	}
	const Key code = find_keycode(p_name);
	known.insert(p_name, code);
	return code;
}

float calibrate(const Dictionary &p_binding, float p_raw) {
	const float from = p_binding.get("from", 0.0);
	const float to = p_binding.get("to", 1.0);
	if (Math::is_equal_approx(from, to)) {
		return 0.0;
	}
	float amount = CLAMP((p_raw - from) / (to - from), 0.0f, 1.0f);
	const float deadzone = p_binding.get("deadzone", 0.0);
	if (deadzone > 0.0f && deadzone < 1.0f) {
		amount = CLAMP((amount - deadzone) / (1.0f - deadzone), 0.0f, 1.0f);
	}
	const float curve = p_binding.get("curve", 1.0);
	return curve == 1.0f ? amount : Math::pow(amount, curve);
}

bool rises(const Dictionary &p_binding) {
	return float(p_binding.get("to", 1.0)) >= float(p_binding.get("from", 0.0));
}

bool is_joypad(const Dictionary &p_binding) {
	return p_binding.has("button") || p_binding.has("axis") || p_binding.has("hat");
}

bool same_input(const Dictionary &p_a, const Dictionary &p_b) {
	if (p_a.has("key") || p_b.has("key")) {
		return p_a.has("key") && p_b.has("key") && keycode(p_a["key"]) == keycode(p_b["key"]);
	}
	if (p_a.has("mouse") || p_b.has("mouse")) {
		return p_a.has("mouse") && p_b.has("mouse") && String(p_a["mouse"]) == String(p_b["mouse"]);
	}
	const String guid_a = p_a.get("guid", "");
	const String guid_b = p_b.get("guid", "");
	const bool same_device = !guid_a.is_empty() && !guid_b.is_empty() ? guid_a == guid_b : String(p_a.get("device", "gamepad")) == String(p_b.get("device", "gamepad"));
	if (!same_device) {
		return false;
	}
	if (p_a.has("button") || p_b.has("button")) {
		return p_a.has("button") && p_b.has("button") && button_index(p_a["button"]) == button_index(p_b["button"]);
	}
	if (p_a.has("hat") || p_b.has("hat")) {
		return p_a.has("hat") && p_b.has("hat") && String(p_a["hat"]) == String(p_b["hat"]);
	}
	if (p_a.has("axis") && p_b.has("axis")) {
		return axis_index(p_a["axis"]) == axis_index(p_b["axis"]) && rises(p_a) == rises(p_b);
	}
	return false;
}

String key_prompt_name(Key p_keycode) {
	return keycode_get_string(p_keycode).to_lower().replace(" ", "_");
}

String prompt_control(const Dictionary &p_binding) {
	if (p_binding.has("key")) {
		return key_prompt_name(keycode(p_binding["key"]));
	}
	if (p_binding.has("mouse")) {
		return p_binding["mouse"];
	}
	if (p_binding.has("hat")) {
		return "dpad_" + String(p_binding["hat"]);
	}
	if (p_binding.has("button")) {
		const int index = button_index(p_binding["button"]);
		return index >= 0 && index < BUTTON_COUNT ? String(BUTTONS[index]) : "button_" + itos(index);
	}
	if (p_binding.has("axis")) {
		const int axis = axis_index(p_binding["axis"]);
		const bool up = rises(p_binding);
		switch (JoyAxis(axis)) {
			case JoyAxis::LEFT_X:
				return up ? "left_stick_right" : "left_stick_left";
			case JoyAxis::LEFT_Y:
				return up ? "left_stick_down" : "left_stick_up";
			case JoyAxis::RIGHT_X:
				return up ? "right_stick_right" : "right_stick_left";
			case JoyAxis::RIGHT_Y:
				return up ? "right_stick_down" : "right_stick_up";
			case JoyAxis::TRIGGER_LEFT:
				return "left_trigger";
			case JoyAxis::TRIGGER_RIGHT:
				return "right_trigger";
			default:
				return "axis_" + itos(axis) + (up ? "_plus" : "_minus");
		}
	}
	return String();
}

String describe(const Dictionary &p_binding, const String &p_key_label) {
	if (p_binding.has("key")) {
		return p_key_label.is_empty() ? String(p_binding["key"]) : p_key_label;
	}
	if (p_binding.has("mouse")) {
		const String name = p_binding["mouse"];
		if (name == "left") {
			return tr("Left mouse button");
		} else if (name == "right") {
			return tr("Right mouse button");
		} else if (name == "middle") {
			return tr("Middle mouse button");
		} else if (name == "wheel_up") {
			return tr("Mouse wheel up");
		} else if (name == "wheel_down") {
			return tr("Mouse wheel down");
		}
		return words("mouse_" + name);
	}
	if (p_binding.has("hat")) {
		return vformat(tr("Hat %s"), tr(String(p_binding["hat"]).capitalize().utf8().get_data()));
	}
	if (p_binding.has("button")) {
		const Variant name = button_name(button_index(p_binding["button"]));
		return name.get_type() == Variant::STRING ? words(name) : vformat(tr("Button %d"), int(name));
	}
	if (p_binding.has("axis")) {
		const String control = prompt_control(p_binding);
		if (control.begins_with("axis_")) {
			return vformat(tr("Axis %d %s"), axis_index(p_binding["axis"]), rises(p_binding) ? "+" : "-");
		}
		return words(control);
	}
	return String();
}

} // namespace InputBinding
