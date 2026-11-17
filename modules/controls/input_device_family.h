#pragma once

// What kind of controller a joypad is, for showing its own button prompts: from its USB
// vendor and product (SDL reports them), its name, and Steam's word on what's really in the
// player's hands when Steam Input stands in for it. Steering wheels' own button names come
// from data/wheel_layouts.json.

#include "core/string/ustring.h"
#include "core/variant/dictionary.h"

namespace InputDeviceFamily {

// Matches InputBindings::DeviceFamily.
enum Family {
	KEYBOARD,
	XBOX,
	PLAYSTATION_4,
	PLAYSTATION_5,
	NINTENDO,
	STEAM_DECK,
	STEERING_WHEEL,
	GENERIC,
};

// A family's name in prompt images (prompt_images.py): "xbox", "playstation5"... Generic pads
// and wheels show Xbox images for their gamepad buttons.
String image_family(Family p_family);

// A joypad's family, from what Godot knows of it (Input::get_joy_info) and its name.
Family of_joypad(int p_vendor_id, int p_product_id, const String &p_name, bool p_steering_wheel);
// The family Steam says a controller is (ESteamInputType), or -1 when Steam doesn't know.
int of_steam_input_type(int p_type);

// "VVVV:PPPP", as the wheel layouts are keyed.
String usb_key(int p_vendor_id, int p_product_id);
// A steering wheel's layout ({name, faces, buttons}), or empty if Purr doesn't know it.
Dictionary wheel_layout(int p_vendor_id, int p_product_id);

} // namespace InputDeviceFamily
