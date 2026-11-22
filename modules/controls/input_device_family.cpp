#include "input_device_family.h"

#include "wheel_layouts.gen.h"

#include "core/io/json.h"

namespace {

const int MICROSOFT = 0x045E;
const int SONY = 0x054C;
const int NINTENDO_VENDOR = 0x057E;
const int VALVE = 0x28DE;
const int DUALSENSE = 0x0CE6;
const int DUALSENSE_EDGE = 0x0DF2;
const int STEAM_DECK_PRODUCT = 0x1205;

// Read when a joypad is plugged in (it's small), never kept in a static: Variants can't outlive
// the engine.
Dictionary wheel_data() {
	return JSON::parse_string(String::utf8(WHEEL_LAYOUTS_JSON));
}

} // namespace

namespace InputDeviceFamily {

String image_family(Family p_family) {
	switch (p_family) {
		case KEYBOARD:
			return "keyboard";
		case PLAYSTATION_4:
			return "playstation4";
		case PLAYSTATION_5:
			return "playstation5";
		case NINTENDO:
			return "nintendo";
		case STEAM_DECK:
			return "steam_deck";
		default:
			return "xbox";
	}
}

Family of_joypad(int p_vendor_id, int p_product_id, const String &p_name, bool p_steering_wheel) {
	if (p_steering_wheel) {
		return STEERING_WHEEL;
	}
	if (p_vendor_id == MICROSOFT) {
		return XBOX;
	}
	if (p_vendor_id == SONY) {
		return p_product_id == DUALSENSE || p_product_id == DUALSENSE_EDGE ? PLAYSTATION_5 : PLAYSTATION_4;
	}
	if (p_vendor_id == NINTENDO_VENDOR) {
		return NINTENDO;
	}
	if (p_vendor_id == VALVE && p_product_id == STEAM_DECK_PRODUCT) {
		return STEAM_DECK;
	}
	// Third-party pads and adapters say what they are in their names.
	const String name = p_name.to_lower();
	if (name.contains("dualsense") || name.contains("ps5")) {
		return PLAYSTATION_5;
	}
	if (name.contains("dualshock") || name.contains("ps4") || name.contains("ps3") || name.contains("playstation")) {
		return PLAYSTATION_4;
	}
	if (name.contains("nintendo") || name.contains("switch") || name.contains("joy-con")) {
		return NINTENDO;
	}
	if (name.contains("steam deck")) {
		return STEAM_DECK;
	}
	if (name.contains("xbox") || name.contains("xinput")) {
		return XBOX;
	}
	return GENERIC;
}

int of_steam_input_type(int p_type) {
	// ESteamInputType, from Steamworks' isteaminput.h.
	switch (p_type) {
		case 2: // Xbox 360
		case 3: // Xbox One
			return XBOX;
		case 5: // PS4
		case 12: // PS3
			return PLAYSTATION_4;
		case 13: // PS5
			return PLAYSTATION_5;
		case 8: // Joy-Con pair
		case 9: // a single Joy-Con
		case 10: // Switch Pro Controller
			return NINTENDO;
		case 14: // Steam Deck
			return STEAM_DECK;
		case 4: // generic gamepad
			return GENERIC;
		default:
			return -1;
	}
}

String usb_key(int p_vendor_id, int p_product_id) {
	return String::num_int64(p_vendor_id, 16, true).lpad(4, "0") + ":" + String::num_int64(p_product_id, 16, true).lpad(4, "0");
}

Dictionary wheel_layout(int p_vendor_id, int p_product_id) {
	const Dictionary data = wheel_data();
	const Dictionary wheels = data.get("wheels", Dictionary());
	const Dictionary wheel = wheels.get(usb_key(p_vendor_id, p_product_id), Dictionary());
	if (wheel.is_empty()) {
		return Dictionary();
	}
	Dictionary layout = Dictionary(Dictionary(data.get("layouts", Dictionary())).get(wheel.get("layout", ""), Dictionary()));
	layout["name"] = wheel.get("name", "");
	return layout;
}

} // namespace InputDeviceFamily
