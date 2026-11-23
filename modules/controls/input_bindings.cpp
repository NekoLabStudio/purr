#include "input_bindings.h"

#include "input_binding.h"
#include "input_bindings_listener.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/input/input_map.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/keyboard.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/gui/rich_text_label.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "servers/display/display_server.h"

InputBindings *InputBindings::singleton = nullptr;

namespace {

// How far an axis must move from where it rested before a capture takes it, so a resting
// pedal isn't picked up by mistake.
const float CAPTURE_THRESHOLD = 0.5;
// Pixels the mouse moves in one event before it counts as the player using it, not a nudge of the desk.
const float MOUSE_MOVED = 2.0;
// What counts as pressed, for an axis bound to an action.
const float PRESSED = 0.5;
// A button press fed in from a wheel's own buttons (where SDL renumbered them).
const char *OWN_META = "_purr_own_button";


int info_int(const Dictionary &p_info, const char *p_key) {
	const Variant value = p_info.get(p_key, Variant());
	return value.get_type() == Variant::STRING ? String(value).to_int() : int(value);
}

Dictionary copy_bindings(const Dictionary &p_bindings) {
	return p_bindings.duplicate(true);
}

// What a key in this place says on the player's keyboard layout, where the platform knows.
Key key_label(Key p_physical) {
	DisplayServer *display = DisplayServer::get_singleton();
	if (!display || display->get_name() == "headless") {
		return Key::NONE;
	}
	return display->keyboard_get_label_from_physical(p_physical);
}

} // namespace

// Starting up, once the scene tree runs (the project's settings and Input Map are loaded).

InputBindings::InputBindings() {
	singleton = this;
	GLOBAL_DEF(PropertyInfo(Variant::STRING, "input_devices/bindings/defaults", PROPERTY_HINT_FILE, "*.json"), "");
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::STRING, "input_devices/prompts/style", PROPERTY_HINT_ENUM, "standard,pixel"), "standard");
	GLOBAL_DEF(PropertyInfo(Variant::INT, "input_devices/prompts/size", PROPERTY_HINT_RANGE, "8,256,1"), 64);
	GLOBAL_DEF(PropertyInfo(Variant::STRING, "input_devices/prompts/custom_set", PROPERTY_HINT_FILE, "*.tres,*.res"), "");
	// The Input Map and project settings are loaded by now, so the defaults are there for the
	// game's own singletons as they start; hooking into the scene tree waits for it to run.
	if (!Engine::get_singleton()->is_editor_hint()) {
		read_defaults();
	}
	callable_mp(this, &InputBindings::start).call_deferred();
}

InputBindings::~InputBindings() {
	singleton = nullptr;
}

void InputBindings::start() {
	if (started || Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	started = true;
	Input::get_singleton()->connect(SNAME("joy_connection_changed"), callable_mp(this, &InputBindings::joy_connection_changed));
	SceneTree *tree = SceneTree::get_singleton();
	if (tree) {
		tree->connect(SNAME("process_frame"), callable_mp(this, &InputBindings::frame));
		tree->get_root()->add_child(memnew(InputBindingsListener), false, Node::INTERNAL_MODE_BACK);
	}
	rebuild();
}

// The game's defaults: its Input Map's own actions (not Godot's ui_*), then the defaults
// file's, which can calibrate axes and have contexts.
void InputBindings::read_defaults() {
	defaults.clear();
	default_contexts.clear();
	InputMap *map = InputMap::get_singleton();
	for (const Variant &name : map->get_actions()) {
		const StringName action = name;
		if (String(action).begins_with("ui_")) {
			continue;
		}
		actions.insert(action, map->action_get_deadzone(action));
		Array bindings;
		for (const Ref<InputEvent> &event : *map->action_get_events(action)) {
			const Dictionary binding = binding_of_event(event, -1);
			if (!binding.is_empty()) {
				if (InputBinding::is_joypad(binding) && binding.has("axis")) {
					Dictionary calibrated = binding;
					calibrated["deadzone"] = Math::snapped(double(map->action_get_deadzone(action)), 0.001);
					bindings.push_back(calibrated);
				} else {
					bindings.push_back(binding);
				}
			}
		}
		defaults[action] = bindings;
	}
	const String path = GLOBAL_GET("input_devices/bindings/defaults");
	if (path.is_empty()) {
		return;
	}
	const Variant file = JSON::parse_string(FileAccess::get_file_as_string(path));
	ERR_FAIL_COND_MSG(file.get_type() != Variant::DICTIONARY, vformat("The controls' defaults (%s) aren't a JSON object.", path));
	const Dictionary data = file;
	const Dictionary bindings = data.get("bindings", Dictionary());
	for (const KeyValue<Variant, Variant> &entry : bindings) {
		const StringName action = entry.key;
		if (!actions.has(action)) {
			if (!map->has_action(action)) {
				map->add_action(action, 0.5);
			}
			actions.insert(action, map->action_get_deadzone(action));
		}
		defaults[action] = entry.value;
	}
	default_contexts = Dictionary(data.get("contexts", Dictionary())).duplicate(true);
	for (const KeyValue<Variant, Variant> &context : default_contexts) {
		for (const KeyValue<Variant, Variant> &entry : Dictionary(context.value)) {
			const StringName action = entry.key;
			if (!actions.has(action)) {
				if (!map->has_action(action)) {
					map->add_action(action, 0.5);
				}
				actions.insert(action, map->action_get_deadzone(action));
			}
		}
	}
}

// Profiles: each player's bindings, the game's defaults until they change something.

InputBindings::Player &InputBindings::player(int p_player) {
	if (!players.has(p_player)) {
		players.insert(p_player, Player());
	}
	return players[p_player];
}

Dictionary &InputBindings::profile_of(int p_player) {
	const String name = player(p_player).profile;
	if (!profiles.has(name)) {
		load_profile(name);
	}
	return profiles[name];
}

String InputBindings::profile_path(const String &p_name) const {
	return save_folder.path_join(p_name.validate_filename() + ".json");
}

// Where players' bindings are saved: the user folder's input_bindings by default, or a game's
// own saves folder (so its tests, or a development copy, keep theirs apart).
void InputBindings::set_save_folder(const String &p_folder) {
	ERR_FAIL_COND_MSG(p_folder.is_empty(), "The controls need a folder to be saved in.");
	if (p_folder == save_folder) {
		return;
	}
	save_folder = p_folder;
	profiles.clear();
	customised.clear();
	rebuild();
	for (const KeyValue<int, Player> &each : players) {
		emit_signal(SNAME("bindings_changed"), each.key);
	}
}

void InputBindings::load_profile(const String &p_name) {
	Dictionary profile;
	profile["bindings"] = copy_bindings(defaults);
	profile["contexts"] = copy_bindings(default_contexts);
	const String path = profile_path(p_name);
	if (FileAccess::exists(path)) {
		const Variant saved = JSON::parse_string(FileAccess::get_file_as_string(path));
		if (saved.get_type() == Variant::DICTIONARY) {
			// What the player saved, for the actions the game still has; the defaults for any
			// action added to the game since.
			Dictionary bindings = profile["bindings"];
			for (const KeyValue<Variant, Variant> &entry : Dictionary(Dictionary(saved).get("bindings", Dictionary()))) {
				if (actions.has(entry.key)) {
					bindings[entry.key] = entry.value;
				}
			}
			Dictionary contexts = profile["contexts"];
			for (const KeyValue<Variant, Variant> &context : Dictionary(Dictionary(saved).get("contexts", Dictionary()))) {
				Dictionary own = contexts.get(context.key, Dictionary());
				for (const KeyValue<Variant, Variant> &entry : Dictionary(context.value)) {
					if (actions.has(entry.key)) {
						own[entry.key] = entry.value;
					}
				}
				contexts[context.key] = own;
			}
			customised.insert(p_name);
		}
	}
	profiles.insert(p_name, profile);
}

void InputBindings::save_profile(const String &p_name) {
	DirAccess::make_dir_recursive_absolute(save_folder);
	Ref<FileAccess> file = FileAccess::open(profile_path(p_name), FileAccess::WRITE);
	ERR_FAIL_COND_MSG(file.is_null(), "Couldn't save the controls.");
	file->store_string(JSON::stringify(profiles[p_name], "\t"));
	customised.insert(p_name);
}

PackedStringArray InputBindings::get_actions() {
	PackedStringArray names;
	for (const KeyValue<StringName, float> &action : actions) {
		names.push_back(action.key);
	}
	names.sort();
	return names;
}

// A player's bindings for one context ("" for those that always apply).
static Dictionary context_bindings(Dictionary &p_profile, const String &p_context) {
	if (p_context.is_empty()) {
		return p_profile["bindings"];
	}
	Dictionary contexts = p_profile["contexts"];
	if (!contexts.has(p_context)) {
		contexts[p_context] = Dictionary();
	}
	return contexts[p_context];
}

TypedArray<Dictionary> InputBindings::get_bindings(const StringName &p_action, int p_player, const String &p_context) {
	return Array(context_bindings(profile_of(p_player), p_context).get(p_action, Array())).duplicate(true);
}

void InputBindings::set_bindings(const StringName &p_action, const TypedArray<Dictionary> &p_bindings, int p_player, const String &p_context) {
	ERR_FAIL_COND_MSG(!actions.has(p_action), vformat("\"%s\" isn't one of the game's actions.", p_action));
	Dictionary bindings = context_bindings(profile_of(p_player), p_context);
	bindings[p_action] = Array(p_bindings).duplicate(true);
	save_profile(player(p_player).profile);
	rebuild();
	emit_signal(SNAME("bindings_changed"), p_player);
}

void InputBindings::bind(const StringName &p_action, const Dictionary &p_binding, int p_player, const String &p_context) {
	TypedArray<Dictionary> bindings = get_bindings(p_action, p_player, p_context);
	for (const Variant &each : bindings) {
		if (InputBinding::same_input(each, p_binding)) {
			return;
		}
	}
	bindings.push_back(p_binding);
	set_bindings(p_action, bindings, p_player, p_context);
}

void InputBindings::unbind(const StringName &p_action, const Dictionary &p_binding, int p_player, const String &p_context) {
	TypedArray<Dictionary> bindings = get_bindings(p_action, p_player, p_context);
	for (int i = bindings.size() - 1; i >= 0; i--) {
		if (InputBinding::same_input(bindings[i], p_binding)) {
			bindings.remove_at(i);
		}
	}
	set_bindings(p_action, bindings, p_player, p_context);
}

TypedArray<Dictionary> InputBindings::get_active_bindings(const StringName &p_action, int p_player) {
	return active_bindings(p_action, p_player).duplicate(true);
}

Dictionary InputBindings::export_bindings(int p_player) {
	return profile_of(p_player).duplicate(true);
}

// A whole set of bindings at once, as export_bindings() gives them (and the defaults file has
// them): {"bindings": {...}, "contexts": {...}}. Actions it leaves out keep the defaults.
void InputBindings::import_bindings(const Dictionary &p_bindings, int p_player) {
	const String name = player(p_player).profile;
	Dictionary profile;
	profile["bindings"] = copy_bindings(defaults);
	profile["contexts"] = copy_bindings(default_contexts);
	Dictionary bindings = profile["bindings"];
	for (const KeyValue<Variant, Variant> &entry : Dictionary(p_bindings.get("bindings", Dictionary()))) {
		if (actions.has(entry.key)) {
			bindings[entry.key] = Array(entry.value).duplicate(true);
		}
	}
	Dictionary contexts = profile["contexts"];
	for (const KeyValue<Variant, Variant> &context : Dictionary(p_bindings.get("contexts", Dictionary()))) {
		Dictionary own = Dictionary(contexts.get(context.key, Dictionary())).duplicate(true);
		for (const KeyValue<Variant, Variant> &entry : Dictionary(context.value)) {
			if (actions.has(entry.key)) {
				own[entry.key] = Array(entry.value).duplicate(true);
			}
		}
		contexts[context.key] = own;
	}
	profiles.insert(name, profile);
	save_profile(name);
	rebuild();
	emit_signal(SNAME("bindings_changed"), p_player);
}

TypedArray<InputEvent> InputBindings::get_binding_events(const Dictionary &p_binding, int p_player) {
	TypedArray<InputEvent> events;
	for (const Ref<InputEvent> &event : events_of(p_binding, p_player)) {
		events.push_back(event);
	}
	return events;
}

bool InputBindings::is_axis_heard(int p_device, int p_axis) {
	return joypad(p_device).heard.has(p_axis);
}

void InputBindings::reset(int p_player) {
	const String name = player(p_player).profile;
	DirAccess::remove_absolute(profile_path(name));
	customised.erase(name);
	profiles.erase(name);
	rebuild();
	emit_signal(SNAME("bindings_changed"), p_player);
}

bool InputBindings::is_customised(int p_player) {
	profile_of(p_player);
	return customised.has(player(p_player).profile);
}

PackedStringArray InputBindings::get_actions_using(const Dictionary &p_binding, int p_player) {
	PackedStringArray using_it;
	for (const KeyValue<StringName, float> &action : actions) {
		for (const Variant &each : active_bindings(action.key, p_player)) {
			if (InputBinding::same_input(each, p_binding)) {
				using_it.push_back(action.key);
				break;
			}
		}
	}
	using_it.sort();
	return using_it;
}

// Contexts: sets of bindings that apply only while the game says (driving a manual, say).

void InputBindings::set_context_active(const String &p_context, bool p_active) {
	if (p_active == active_contexts.has(p_context)) {
		return;
	}
	if (p_active) {
		active_contexts.insert(p_context);
	} else {
		active_contexts.erase(p_context);
	}
	rebuild();
	for (const KeyValue<int, Player> &each : players) {
		emit_signal(SNAME("bindings_changed"), each.key);
	}
}

bool InputBindings::is_context_active(const String &p_context) const {
	return active_contexts.has(p_context);
}

Array InputBindings::active_bindings(const StringName &p_action, int p_player) {
	Dictionary &profile = profile_of(p_player);
	Array bindings = Array(Dictionary(profile["bindings"]).get(p_action, Array())).duplicate();
	const Dictionary contexts = profile["contexts"];
	for (const String &context : active_contexts) {
		bindings.append_array(Dictionary(contexts.get(context, Dictionary())).get(p_action, Array()));
	}
	return bindings;
}

// Players and their devices. Player 1 (0) has the keyboard and every joypad nobody else has.

void InputBindings::set_player_profile(int p_player, const String &p_profile) {
	ERR_FAIL_COND_MSG(p_profile.is_empty(), "A controls profile needs a name.");
	player(p_player).profile = p_profile;
	if (p_player == 0) {
		rebuild();
	}
	emit_signal(SNAME("bindings_changed"), p_player);
}

String InputBindings::get_player_profile(int p_player) {
	return player(p_player).profile;
}

void InputBindings::set_player_devices(int p_player, const PackedInt32Array &p_devices) {
	// A device belongs to one player: given to this one, it's taken from any other.
	for (KeyValue<int, Player> &each : players) {
		if (each.key == p_player) {
			continue;
		}
		for (int device : p_devices) {
			const int at = each.value.devices.find(device);
			if (at >= 0) {
				each.value.devices.remove_at(at);
			}
		}
	}
	Player &owner = player(p_player);
	owner.devices = p_devices;
	// None gives the player back what they have by default.
	owner.assigned = !p_devices.is_empty();
	settle_last_devices();
	rebuild();
	emit_signal(SNAME("devices_changed"));
}

bool InputBindings::player_has(int p_player, int p_device) const {
	for (const KeyValue<int, Player> &each : players) {
		if (each.value.assigned && each.value.devices.has(p_device)) {
			return each.key == p_player;
		}
	}
	// Nobody was given it: player 1's, unless player 1 was given devices of their own.
	return p_player == 0 && !first_player_assigned();
}

void InputBindings::use_device(int p_player, int p_device) {
	const DeviceFamily before = get_player_family(p_player);
	player(p_player).last_device = p_device;
	if (get_player_family(p_player) != before) {
		emit_signal(SNAME("player_family_changed"), p_player, get_player_family(p_player));
	}
}

// A player whose last device went to someone else goes back to the one they used before (the
// press that joined player 2 on a spare controller was player 1's until it was given away),
// else the keyboard, else the first they were given.
void InputBindings::settle_last_devices() {
	for (KeyValue<int, Player> &each : players) {
		const Player &one = each.value;
		if (player_has(each.key, one.last_device)) {
			continue;
		}
		int device = one.devices.is_empty() ? KEYBOARD : one.devices[0];
		if (player_has(each.key, one.previous_device)) {
			device = one.previous_device;
		} else if (player_has(each.key, KEYBOARD)) {
			device = KEYBOARD;
		}
		use_device(each.key, device);
	}
}

bool InputBindings::first_player_assigned() const {
	const Player *first = players.getptr(0);
	return first && first->assigned;
}

PackedInt32Array InputBindings::get_player_devices(int p_player) {
	PackedInt32Array devices;
	if (player_has(p_player, KEYBOARD)) {
		devices.push_back(KEYBOARD);
	}
	for (const Variant &device : Input::get_singleton()->get_connected_joypads()) {
		if (player_has(p_player, device)) {
			devices.push_back(device);
		}
	}
	return devices;
}

int InputBindings::get_device_player(int p_device) {
	for (const KeyValue<int, Player> &each : players) {
		if (each.value.assigned && each.value.devices.has(p_device)) {
			return each.key;
		}
	}
	return first_player_assigned() ? -1 : 0;
}

int InputBindings::get_player_last_device(int p_player) {
	return player(p_player).last_device;
}

InputBindings::DeviceFamily InputBindings::get_player_family(int p_player) {
	const int device = player(p_player).last_device;
	if (device != KEYBOARD && !Input::get_singleton()->get_connected_joypads().has(device)) {
		return player_has(p_player, KEYBOARD) ? FAMILY_KEYBOARD : FAMILY_GENERIC;
	}
	return get_device_family(device);
}

// Joypads: what each is, worked out once.

InputBindings::Joypad &InputBindings::joypad(int p_device) {
	Joypad &pad = joypads[p_device];
	Input *input = Input::get_singleton();
	// Worked out once it's plugged in: until then there's nothing to ask about it.
	if (pad.known || !input->get_connected_joypads().has(p_device)) {
		return pad;
	}
	pad.known = true;
	const Dictionary info = input->get_joy_info(p_device);
	const int vendor = info_int(info, "vendor_id");
	const int product = info_int(info, "product_id");
	const String name = input->get_joy_name(p_device);
	pad.layout = InputDeviceFamily::wheel_layout(vendor, product);
	pad.wheel = !pad.layout.is_empty() || name.to_lower().contains("wheel");
	if (!pad.wheel && vendor && ClassDB::class_exists("SteeringWheel")) {
		const Ref<RefCounted> wheels = Object::cast_to<RefCounted>(ClassDB::instantiate("SteeringWheel"));
		if (wheels.is_valid()) {
			// find_wheel() only prefers these IDs, falling back on any wheel it can drive: this pad is
			// a wheel only if the wheel found is this one.
			const Dictionary found = wheels->call(SNAME("find_wheel"), vendor, product);
			pad.wheel = int(found.get("vendor_id", -1)) == vendor && int(found.get("product_id", -1)) == product;
		}
	}
	pad.family = DeviceFamily(InputDeviceFamily::of_joypad(vendor, product, name, pad.wheel));
	// Steam Input stands in for the controller (a virtual Xbox pad): Steam knows what it is.
	if (!pad.wheel && info.has("steam_input_index") && Engine::get_singleton()->has_singleton("Steam")) {
		Object *steam = Engine::get_singleton()->get_singleton_object("Steam");
		if (steam->has_method(SNAME("isSteamRunning")) && bool(steam->call(SNAME("isSteamRunning")))) {
			steam->call(SNAME("inputInit"), false);
			const int family = InputDeviceFamily::of_steam_input_type(steam->call(SNAME("getInputTypeForHandle"), String(info["steam_input_index"]).to_int()));
			if (family >= 0) {
				pad.family = DeviceFamily(family);
			}
		}
	}
	return pad;
}

InputBindings::DeviceFamily InputBindings::get_device_family(int p_device) {
	return p_device == KEYBOARD ? FAMILY_KEYBOARD : joypad(p_device).family;
}

String InputBindings::get_device_name(int p_device) {
	if (p_device == KEYBOARD) {
		return RTR("Keyboard and mouse");
	}
	const Joypad &pad = joypad(p_device);
	return pad.layout.has("name") ? String(pad.layout["name"]) : Input::get_singleton()->get_joy_name(p_device);
}

bool InputBindings::is_steering_wheel(int p_device) {
	return p_device != KEYBOARD && joypad(p_device).wheel;
}

// Whether a joypad's buttons and axes come in SDL's gamepad layout (and have its names).
bool InputBindings::reads_as_gamepad(int p_device) {
	return Input::get_singleton()->is_joy_known(p_device) && !is_steering_wheel(p_device);
}

void InputBindings::joy_connection_changed(int p_device, bool p_connected) {
	joypads.erase(p_device); // Read afresh: another controller may have its number now.
	rebuild();
	emit_signal(SNAME("devices_changed"));
}

// Which joypads a binding means, for a player: the one it names (by GUID, else by name), or for
// "gamepad" every controller SDL knows the layout of, but never a steering wheel (SDL maps some
// as a gamepad with their pedals as triggers, at rest fully pressed). A wheel's axes are bound
// to it by name. p_player -1: anyone's.
PackedInt32Array InputBindings::devices_of(const Dictionary &p_binding, int p_player) {
	PackedInt32Array found;
	if (!InputBinding::is_joypad(p_binding)) {
		return found;
	}
	Input *input = Input::get_singleton();
	const String wanted = p_binding.get("device", "gamepad");
	const String guid = p_binding.get("guid", "");
	for (const Variant &each : input->get_connected_joypads()) {
		const int device = each;
		if (p_player >= 0 && !player_has(p_player, device)) {
			continue;
		}
		if (wanted == "gamepad") {
			if (input->is_joy_known(device) && !is_steering_wheel(device)) {
				found.push_back(device);
			}
		} else if (!guid.is_empty() && input->get_joy_guid(device) == guid) {
			found.clear();
			found.push_back(device);
			return found;
		}
	}
	if (wanted != "gamepad" && found.is_empty()) {
		for (const Variant &each : input->get_connected_joypads()) {
			const int device = each;
			if ((p_player < 0 || player_has(p_player, device)) && input->get_joy_name(device) == wanted) {
				found.push_back(device);
				return found;
			}
		}
	}
	return found;
}

PackedInt32Array InputBindings::get_binding_devices(const Dictionary &p_binding, int p_player) {
	return devices_of(p_binding, p_player);
}

PackedInt32Array InputBindings::find_devices(const Dictionary &p_binding) {
	return devices_of(p_binding, -1);
}

Dictionary InputBindings::device_fields(int p_device) {
	Dictionary fields;
	fields["device"] = Input::get_singleton()->get_joy_name(p_device);
	fields["guid"] = Input::get_singleton()->get_joy_guid(p_device);
	return fields;
}

// Reading.

float InputBindings::read(const Dictionary &p_binding, int p_player) {
	Input *input = Input::get_singleton();
	if (p_binding.has("key")) {
		return player_has(p_player, KEYBOARD) && input->is_physical_key_pressed(InputBinding::keycode(p_binding["key"])) ? 1.0 : 0.0;
	}
	if (p_binding.has("mouse")) {
		const MouseButton button = InputBinding::mouse_button(p_binding["mouse"]);
		if (!player_has(p_player, KEYBOARD) || button == MouseButton::NONE) {
			return 0.0;
		}
		return input->is_mouse_button_pressed(button) || wheel_turns.has(int(button)) ? 1.0 : 0.0;
	}
	float strongest = 0.0;
	for (int device : devices_of(p_binding, p_player)) {
		Joypad &pad = joypad(device);
		if (p_binding.has("hat")) {
			if (pad.from_hat.has(InputBinding::dpad_button(p_binding["hat"]))) {
				strongest = 1.0;
			}
		} else if (p_binding.has("button")) {
			const int index = InputBinding::button_index(p_binding["button"]);
			const bool held = pad.own_buttons ? pad.own_held.has(index) : input->is_joy_button_pressed(device, JoyButton(index)) && !pad.from_hat.has(index);
			if (held) {
				strongest = 1.0;
			}
		} else if (p_binding.has("axis")) {
			const int axis = InputBinding::axis_index(p_binding["axis"]);
			if (pad.heard.has(axis)) {
				strongest = MAX(strongest, InputBinding::calibrate(p_binding, input->get_joy_axis(device, JoyAxis(axis))));
			}
		}
	}
	return strongest;
}

float InputBindings::get_binding_value(const Dictionary &p_binding, int p_player) {
	return read(p_binding, p_player);
}

float InputBindings::get_value(const StringName &p_action, int p_player) {
	float strongest = p_player == 0 ? pressed_by_game(p_action) : 0.0;
	for (const Variant &binding : active_bindings(p_action, p_player)) {
		strongest = MAX(strongest, read(binding, p_player));
	}
	return strongest;
}

// What the game pressed itself, which Input knows and the bindings don't: Input.action_press(),
// or an InputEventAction (on-screen buttons, Apple TV's Menu button). The Input Map is player 1's,
// so those are too. Input's action is as strong as its strongest source; when that's stronger
// than any of the Input Map's events can be now, the rest came from the game.
float InputBindings::pressed_by_game(const StringName &p_action) {
	Input *input = Input::get_singleton();
	InputMap *map = InputMap::get_singleton();
	if (!map->has_action(p_action) || !input->is_action_pressed(p_action)) {
		return 0.0;
	}
	float from_events = 0.0;
	for (const Ref<InputEvent> &event : *map->action_get_events(p_action)) {
		const Ref<InputEventKey> key = event;
		const Ref<InputEventMouseButton> mouse = event;
		const Ref<InputEventJoypadButton> button = event;
		const Ref<InputEventJoypadMotion> motion = event;
		if (key.is_valid() && input->is_physical_key_pressed(key->get_physical_keycode())) {
			from_events = 1.0;
		} else if (mouse.is_valid() && input->is_mouse_button_pressed(mouse->get_button_index())) {
			from_events = 1.0;
		} else if (button.is_valid() && input->is_joy_button_pressed(button->get_device(), button->get_button_index())) {
			from_events = 1.0;
		} else if (motion.is_valid()) {
			const float along = input->get_joy_axis(motion->get_device(), motion->get_axis()) * SIGN(motion->get_axis_value());
			from_events = MAX(from_events, along);
		}
	}
	return input->get_action_raw_strength(p_action) > from_events + CMP_EPSILON ? input->get_action_strength(p_action) : 0.0;
}

bool InputBindings::is_pressed(const StringName &p_action, int p_player) {
	return get_value(p_action, p_player) >= PRESSED;
}

// Whether a press or release recorded for this frame and tick is now, where the game is asking:
// its physics or its drawing.
bool InputBindings::happened_now(uint64_t p_frame, uint64_t p_tick) const {
	const Engine *engine = Engine::get_singleton();
	return engine->is_in_physics_frame() ? p_tick == engine->get_physics_frames() : p_frame == engine->get_process_frames();
}

bool InputBindings::is_just_pressed(const StringName &p_action, int p_player) {
	const Presses *presses = player(p_player).presses.getptr(p_action);
	return presses && happened_now(presses->pressed_frame, presses->pressed_tick);
}

bool InputBindings::is_just_released(const StringName &p_action, int p_player) {
	const Presses *presses = player(p_player).presses.getptr(p_action);
	return presses && happened_now(presses->released_frame, presses->released_tick);
}

float InputBindings::get_axis(const StringName &p_negative, const StringName &p_positive, int p_player) {
	return get_value(p_positive, p_player) - get_value(p_negative, p_player);
}

Vector2 InputBindings::get_vector(const StringName &p_negative_x, const StringName &p_positive_x, const StringName &p_negative_y, const StringName &p_positive_y, int p_player) {
	const Vector2 vector(get_axis(p_negative_x, p_positive_x, p_player), get_axis(p_negative_y, p_positive_y, p_player));
	return vector.length() > 1.0 ? vector.normalized() : vector;
}

bool InputBindings::has_axis(const StringName &p_action, int p_player) {
	for (const Variant &binding : active_bindings(p_action, p_player)) {
		if (Dictionary(binding).has("axis") && !devices_of(binding, p_player).is_empty()) {
			return true;
		}
	}
	return false;
}

bool InputBindings::is_digital(const StringName &p_action, int p_player) {
	float strongest = 0.0;
	bool digital = false;
	for (const Variant &binding : active_bindings(p_action, p_player)) {
		const float amount = read(binding, p_player);
		if (amount > strongest) {
			strongest = amount;
			digital = !Dictionary(binding).has("axis");
		}
	}
	return digital;
}

// Player 1's bindings into Godot's InputMap, each joypad binding on the devices it means.

Vector<Ref<InputEvent>> InputBindings::events_of(const Dictionary &p_binding, int p_player) {
	Vector<Ref<InputEvent>> events;
	if (p_binding.has("key")) {
		if (player_has(p_player, KEYBOARD)) {
			Ref<InputEventKey> key;
			key.instantiate();
			key->set_physical_keycode(InputBinding::keycode(p_binding["key"]));
			events.push_back(key);
		}
		return events;
	}
	if (p_binding.has("mouse")) {
		if (player_has(p_player, KEYBOARD)) {
			Ref<InputEventMouseButton> mouse;
			mouse.instantiate();
			mouse->set_button_index(InputBinding::mouse_button(p_binding["mouse"]));
			events.push_back(mouse);
		}
		return events;
	}
	for (int device : devices_of(p_binding, p_player)) {
		if (p_binding.has("button") || p_binding.has("hat")) {
			Ref<InputEventJoypadButton> button;
			button.instantiate();
			button->set_device(device);
			button->set_button_index(JoyButton(p_binding.has("hat") ? InputBinding::hat_button(p_binding["hat"]) : InputBinding::button_index(p_binding["button"])));
			events.push_back(button);
		} else if (p_binding.has("axis")) {
			Ref<InputEventJoypadMotion> motion;
			motion.instantiate();
			motion->set_device(device);
			motion->set_axis(JoyAxis(InputBinding::axis_index(p_binding["axis"])));
			motion->set_axis_value(InputBinding::rises(p_binding) ? 1.0 : -1.0);
			events.push_back(motion);
		}
	}
	return events;
}

TypedArray<InputEvent> InputBindings::get_events(const StringName &p_action, int p_player) {
	TypedArray<InputEvent> events;
	for (const Variant &binding : active_bindings(p_action, p_player)) {
		for (const Ref<InputEvent> &event : events_of(binding, p_player)) {
			events.push_back(event);
		}
	}
	return events;
}

void InputBindings::rebuild() {
	if (!started) {
		return;
	}
	InputMap *map = InputMap::get_singleton();
	for (const KeyValue<StringName, float> &action : actions) {
		if (!map->has_action(action.key)) {
			map->add_action(action.key, action.value);
		}
		map->action_erase_events(action.key);
		for (const Variant &binding : active_bindings(action.key, 0)) {
			for (const Ref<InputEvent> &event : events_of(binding, 0)) {
				map->action_add_event(action.key, event);
			}
		}
	}
	put_back_ui_events();
}

// Godot's own menu movement (ui_up and the rest) answers every joypad's D-pad and stick, a
// steering wheel's too: its steering and pedals moved the highlight round the menus. Those
// defaults are kept for controllers only, each by its own device.
void InputBindings::put_back_ui_events() {
	InputMap *map = InputMap::get_singleton();
	if (ui_defaults.is_empty()) {
		for (const Variant &name : map->get_actions()) {
			const StringName action = name;
			if (!String(action).begins_with("ui_")) {
				continue;
			}
			Vector<Ref<InputEvent>> events;
			for (const Ref<InputEvent> &event : *map->action_get_events(action)) {
				events.push_back(event);
			}
			for (const Ref<InputEvent> &event : events) {
				const Ref<InputEventJoypadButton> button = event;
				const Ref<InputEventJoypadMotion> motion = event;
				if ((button.is_valid() || motion.is_valid()) && event->get_device() == InputMap::ALL_DEVICES) {
					ui_defaults[action].push_back(event);
					map->action_erase_event(action, event);
				}
			}
		}
	}
	for (const Pair<StringName, Ref<InputEvent>> &added : ui_added) {
		if (map->has_action(added.first)) {
			map->action_erase_event(added.first, added.second);
		}
	}
	ui_added.clear();
	Input *input = Input::get_singleton();
	for (const Variant &each : input->get_connected_joypads()) {
		const int device = each;
		if (!input->is_joy_known(device) || is_steering_wheel(device)) {
			continue;
		}
		for (const KeyValue<StringName, Vector<Ref<InputEvent>>> &action : ui_defaults) {
			for (const Ref<InputEvent> &event : action.value) {
				Ref<InputEvent> copy = event->duplicate();
				copy->set_device(device);
				map->action_add_event(action.key, copy);
				ui_added.push_back(Pair<StringName, Ref<InputEvent>>(action.key, copy));
			}
		}
	}
}

// Steering wheels' hats. Godot reports a hat as joypad buttons 11 to 14, the same numbers as
// some wheels' own buttons (a G923's shifter's first gears): each press there is told apart as
// it comes in (SteeringWheelHats says whether the hat is held that way) and the hat's moved to
// buttons of its own. Where SDL reads a wheel as a gamepad and renumbers its buttons (Windows),
// its own buttons and hat are read by SteeringWheelHats instead and fed in by their own numbers.

int InputBindings::hat_of(int p_device) {
	Joypad &pad = joypad(p_device);
	if (!pad.hats_checked && pad.wheel && ClassDB::class_exists("SteeringWheelHats")) {
		pad.hats_checked = true;
		const Dictionary info = Input::get_singleton()->get_joy_info(p_device);
		const Ref<RefCounted> reader = Object::cast_to<RefCounted>(ClassDB::instantiate("SteeringWheelHats"));
		if (reader.is_valid() && bool(reader->call(SNAME("watch"), info_int(info, "vendor_id"), info_int(info, "product_id")))) {
			pad.hats = reader;
			pad.own_buttons = Input::get_singleton()->is_joy_known(p_device) && bool(reader->call(SNAME("reads_buttons")));
		}
	}
	return pad.hats.is_valid() ? int(pad.hats->call(SNAME("get_hat"))) : -1;
}

void InputBindings::feed_own_buttons(int p_device) {
	Joypad &pad = joypad(p_device);
	HashSet<int> held;
	for (int button : PackedInt32Array(pad.hats->call(SNAME("get_buttons")))) {
		held.insert(button);
	}
	Input *input = Input::get_singleton();
	auto press = [&](int p_button, bool p_pressed) {
		Ref<InputEventJoypadButton> event;
		event.instantiate();
		event->set_device(p_device);
		event->set_button_index(JoyButton(p_button));
		event->set_pressed(p_pressed);
		event->set_meta(OWN_META, true);
		input->parse_input_event(event);
	};
	for (int button : held) {
		if (!pad.own_held.has(button)) {
			press(button, true);
		}
	}
	for (int button : pad.own_held) {
		if (!held.has(button)) {
			press(button, false);
		}
	}
	pad.own_held = held;
	const int hat = MAX(int(pad.hats->call(SNAME("get_hat"))), 0);
	for (const char *direction : { "up", "right", "down", "left" }) {
		const int mask = InputBinding::hat_mask(direction);
		if ((hat & mask) != (pad.own_hat & mask)) {
			if (hat & mask) {
				pad.from_hat.insert(InputBinding::dpad_button(direction));
			} else {
				pad.from_hat.erase(InputBinding::dpad_button(direction));
			}
			press(InputBinding::hat_button(direction), (hat & mask) != 0);
		}
	}
	pad.own_hat = hat;
}

void InputBindings::tell_hat_apart(const Ref<InputEvent> &p_event) {
	Ref<InputEventJoypadButton> press = p_event;
	if (press.is_null() || !is_steering_wheel(press->get_device())) {
		return;
	}
	const int device = press->get_device();
	InputMap *map = InputMap::get_singleton();
	Input *input = Input::get_singleton();
	hat_of(device);
	Joypad &pad = joypad(device);
	if (pad.own_buttons) {
		// A press as SDL renumbered it: let go of what it held, and made a press of nothing.
		if (!press->has_meta(OWN_META)) {
			for (const Variant &action : map->get_actions()) {
				if (map->event_is_action(press, action, true)) {
					input->action_release(action);
				}
			}
			press->set_button_index(JoyButton(InputBinding::DROPPED_BUTTON));
			press->set_pressed(false);
		}
		return;
	}
	const String direction = InputBinding::dpad_direction(int(press->get_button_index()));
	if (direction.is_empty()) {
		return;
	}
	const int index = int(press->get_button_index());
	if (press->is_pressed()) {
		const int hat = hat_of(device);
		if (hat < 0 || (hat & InputBinding::hat_mask(direction)) == 0) {
			pad.from_hat.erase(index);
			return;
		}
		pad.from_hat.insert(index);
	} else if (!pad.from_hat.has(index)) {
		return;
	} else {
		pad.from_hat.erase(index);
	}
	// Godot has held the actions of the button it came as by now: let go, and the hat's held.
	const Ref<InputEventJoypadButton> came_as = press->duplicate();
	press->set_button_index(JoyButton(InputBinding::hat_button(direction)));
	for (const Variant &action : map->get_actions()) {
		if (map->event_is_action(came_as, action, true)) {
			input->action_release(action);
		}
		if (map->event_is_action(press, action, true)) {
			if (press->is_pressed()) {
				input->action_press(action);
			} else {
				input->action_release(action);
			}
		}
	}
}

// Every input event, first.

bool InputBindings::input(const Ref<InputEvent> &p_event) {
	tell_hat_apart(p_event);
	const Ref<InputEventKey> key = p_event;
	const Ref<InputEventMouseButton> mouse = p_event;
	const Ref<InputEventJoypadButton> button = p_event;
	const Ref<InputEventJoypadMotion> motion = p_event;
	const Ref<InputEventMouseMotion> mouse_motion = p_event;
	if (motion.is_valid()) {
		joypad(motion->get_device()).heard.insert(int(motion->get_axis()));
	}
	if (mouse.is_valid() && mouse->is_pressed() && (mouse->get_button_index() == MouseButton::WHEEL_UP || mouse->get_button_index() == MouseButton::WHEEL_DOWN || mouse->get_button_index() == MouseButton::WHEEL_LEFT || mouse->get_button_index() == MouseButton::WHEEL_RIGHT)) {
		wheel_turns.insert(int(mouse->get_button_index()));
	}

	// The controller each player used last, for their prompts.
	int used = -2;
	if ((key.is_valid() && key->is_pressed()) || (mouse.is_valid() && mouse->is_pressed()) || (mouse_motion.is_valid() && mouse_motion->get_relative().length() > MOUSE_MOVED)) {
		used = KEYBOARD;
	} else if ((button.is_valid() && button->is_pressed()) || (motion.is_valid() && Math::abs(motion->get_axis_value()) > CAPTURE_THRESHOLD)) {
		used = p_event->get_device();
	}
	if (mouse_motion.is_null()) {
		track_presses();
	}

	const int owner = used != -2 ? get_device_player(used) : -1;
	if (owner >= 0 && player(owner).last_device != used) {
		player(owner).previous_device = player(owner).last_device;
		use_device(owner, used);
	}

	if (capture_call.is_null()) {
		return false;
	}
	Dictionary binding;
	if (key.is_valid() && key->is_pressed() && !key->is_echo() && player_has(capture_player, KEYBOARD)) {
		if (key->get_physical_keycode() == Key::ESCAPE) {
			Dictionary cancelled;
			cancelled["ok"] = false;
			cancelled["error"] = "cancelled";
			cancelled["message"] = "The player cancelled.";
			finish_capture(cancelled);
			return true;
		}
		binding["key"] = keycode_get_string(key->get_physical_keycode());
	} else if (mouse.is_valid() && mouse->is_pressed() && player_has(capture_player, KEYBOARD)) {
		binding["mouse"] = InputBinding::mouse_name(mouse->get_button_index());
	} else if (button.is_valid() && button->is_pressed() && player_has(capture_player, button->get_device()) && int(button->get_button_index()) != InputBinding::DROPPED_BUTTON) {
		binding = device_fields(button->get_device());
		const String hat = InputBinding::hat_direction(int(button->get_button_index()));
		if (!hat.is_empty()) {
			binding["hat"] = hat;
		} else {
			// Named in a controller's layout; a wheel's or another joystick's own numbers otherwise.
			const int index = int(button->get_button_index());
			binding["button"] = reads_as_gamepad(button->get_device()) ? InputBinding::button_name(index) : Variant(index);
		}
	} else if (motion.is_valid() && player_has(capture_player, motion->get_device())) {
		const Vector2i at(motion->get_device(), int(motion->get_axis()));
		const float rest = capture_rest.has(at) ? capture_rest[at] : 0.0f;
		if (Math::abs(motion->get_axis_value() - rest) < CAPTURE_THRESHOLD) {
			return false;
		}
		binding = device_fields(motion->get_device());
		binding["axis"] = reads_as_gamepad(motion->get_device()) ? InputBinding::axis_name(int(motion->get_axis())) : Variant(int(motion->get_axis()));
		binding["from"] = Math::snapped(rest, 0.01f);
		binding["to"] = motion->get_axis_value() > rest ? 1.0 : -1.0;
	}
	if (binding.is_empty() || (binding.has("mouse") && String(binding["mouse"]).is_empty())) {
		return false;
	}
	Dictionary result;
	result["ok"] = true;
	result["binding"] = binding;
	finish_capture(result);
	return true;
}

Signal InputBindings::capture(int p_player) {
	cancel_capture();
	capture_call.instantiate();
	capture_player = p_player;
	capture_rest.clear();
	Input *input = Input::get_singleton();
	for (const Variant &each : input->get_connected_joypads()) {
		const int device = each;
		for (int axis = 0; axis < int(JoyAxis::MAX); axis++) {
			capture_rest[Vector2i(device, axis)] = input->get_joy_axis(device, JoyAxis(axis));
		}
	}
	return Signal(capture_call.ptr(), SNAME("completed"));
}

void InputBindings::cancel_capture() {
	if (capture_call.is_null()) {
		return;
	}
	Dictionary cancelled;
	cancelled["ok"] = false;
	cancelled["error"] = "cancelled";
	cancelled["message"] = "Capturing was cancelled.";
	finish_capture(cancelled);
}

void InputBindings::finish_capture(const Dictionary &p_result) {
	const Ref<InputBindingsCall> call = capture_call;
	capture_call.unref();
	call->complete_later(p_result);
}

Dictionary InputBindings::binding_of_event(const Ref<InputEvent> &p_event, int p_device) {
	Dictionary binding;
	const Ref<InputEventKey> key = p_event;
	const Ref<InputEventMouseButton> mouse = p_event;
	const Ref<InputEventJoypadButton> button = p_event;
	const Ref<InputEventJoypadMotion> motion = p_event;
	if (key.is_valid()) {
		const Key code = key->get_physical_keycode() != Key::NONE ? key->get_physical_keycode() : key->get_keycode();
		if (code != Key::NONE) {
			binding["key"] = keycode_get_string(code);
		}
	} else if (mouse.is_valid()) {
		const String name = InputBinding::mouse_name(mouse->get_button_index());
		if (!name.is_empty()) {
			binding["mouse"] = name;
		}
	} else if (button.is_valid()) {
		binding = p_device >= 0 ? device_fields(p_device) : Dictionary();
		binding["device"] = p_device >= 0 ? binding["device"] : Variant("gamepad");
		binding["button"] = InputBinding::button_name(int(button->get_button_index()));
	} else if (motion.is_valid()) {
		binding = p_device >= 0 ? device_fields(p_device) : Dictionary();
		binding["device"] = p_device >= 0 ? binding["device"] : Variant("gamepad");
		binding["axis"] = InputBinding::axis_name(int(motion->get_axis()));
		binding["from"] = 0.0;
		binding["to"] = motion->get_axis_value() < 0 ? -1.0 : 1.0;
	}
	return binding;
}

void InputBindings::frame() {
	Input *input = Input::get_singleton();
	for (const Variant &each : input->get_connected_joypads()) {
		const int device = each;
		if (is_steering_wheel(device)) {
			hat_of(device);
			if (joypad(device).own_buttons) {
				feed_own_buttons(device);
			}
		}
	}
	wheel_turns.clear();
	track_presses();
}

// Notes what's pressed and released, as each input arrives and once a frame (for what changes
// without an event, such as a mouse wheel's turn ending). Like Input, a press counts for this
// frame and the next physics tick, so a tap shorter than a tick is still seen there.
void InputBindings::track_presses() {
	const uint64_t frame = Engine::get_singleton()->get_process_frames();
	const uint64_t tick = Engine::get_singleton()->get_physics_frames() + 1;
	player(0);
	for (KeyValue<int, Player> &each : players) {
		Player &one = each.value;
		for (const KeyValue<StringName, float> &action : actions) {
			const bool held = get_value(action.key, each.key) >= PRESSED;
			if (held == one.held.has(action.key)) {
				continue;
			}
			Presses &presses = one.presses[action.key];
			if (held) {
				one.held.insert(action.key);
				presses.pressed_frame = frame;
				presses.pressed_tick = tick;
			} else {
				one.held.erase(action.key);
				presses.released_frame = frame;
				presses.released_tick = tick;
			}
		}
	}
}

// Words and prompts.

String InputBindings::describe_binding(const Dictionary &p_binding) {
	if (p_binding.has("key")) {
		const Key label = key_label(InputBinding::keycode(p_binding["key"]));
		return InputBinding::describe(p_binding, label != Key::NONE ? keycode_get_string(label) : String());
	}
	if (p_binding.has("button")) {
		for (int device : find_devices(p_binding)) {
			const Joypad &pad = joypad(device);
			if (!pad.layout.is_empty() && !Input::get_singleton()->is_joy_known(device)) {
				const Dictionary own = Dictionary(pad.layout.get("buttons", Dictionary())).get(itos(InputBinding::button_index(p_binding["button"])), Dictionary());
				if (own.has("name")) {
					return RTR(String(own["name"]));
				}
			}
		}
	}
	return InputBinding::describe(p_binding);
}

// The binding a player would use for an action now: on the keyboard if that's what they used
// last, else on the controller they used last.
Dictionary InputBindings::get_prompt_binding(const StringName &p_action, int p_player) {
	const Array bindings = active_bindings(p_action, p_player);
	if (bindings.is_empty()) {
		return Dictionary();
	}
	const int device = player(p_player).last_device;
	const bool keyboard = get_player_family(p_player) == FAMILY_KEYBOARD;
	for (const Variant &each : bindings) {
		const Dictionary binding = each;
		if (keyboard ? !InputBinding::is_joypad(binding) : devices_of(binding, p_player).has(device)) {
			return binding;
		}
	}
	for (const Variant &each : bindings) {
		if (keyboard ? true : !devices_of(each, p_player).is_empty()) {
			return each;
		}
	}
	return bindings[0];
}

String InputBindings::describe(const StringName &p_action, int p_player) {
	const Dictionary binding = get_prompt_binding(p_action, p_player);
	return binding.is_empty() ? String() : describe_binding(binding);
}

Ref<Texture2D> InputBindings::get_binding_prompt(const Dictionary &p_binding, int p_player, int p_size) {
	const int size = p_size > 0 ? p_size : int(GLOBAL_GET("input_devices/prompts/size"));
	if (p_binding.has("key")) {
		const Key physical = InputBinding::keycode(p_binding["key"]);
		const Key label = key_label(physical);
		return images.get("keyboard/" + InputBinding::key_prompt_name(label != Key::NONE ? label : physical), size);
	}
	if (p_binding.has("mouse")) {
		return images.get("mouse/" + String(p_binding["mouse"]), size);
	}
	if (!InputBinding::is_joypad(p_binding)) {
		return Ref<Texture2D>();
	}
	// On the controller the binding is on (the player's own, else anyone's), else the family the
	// player is using, else Xbox's.
	PackedInt32Array devices = devices_of(p_binding, p_player);
	if (devices.is_empty()) {
		devices = find_devices(p_binding);
	}
	const int last = player(p_player).last_device;
	const int device = devices.has(last) ? last : (devices.is_empty() ? -2 : devices[0]);
	DeviceFamily family = device >= 0 ? get_device_family(device) : get_player_family(p_player);
	if (family == FAMILY_KEYBOARD) {
		family = FAMILY_XBOX;
	}
	const String control = InputBinding::prompt_control(p_binding);
	if (family == FAMILY_STEERING_WHEEL && device >= 0) {
		const Joypad &pad = joypad(device);
		const String faces = pad.layout.get("faces", "xbox");
		if (Input::get_singleton()->is_joy_known(device) || p_binding.has("hat")) {
			// Read as a gamepad: its buttons come in that layout, wearing its own faces.
			return images.get(faces + "/" + control, size);
		}
		if (p_binding.has("button")) {
			const Dictionary own = Dictionary(pad.layout.get("buttons", Dictionary())).get(itos(InputBinding::button_index(p_binding["button"])), Dictionary());
			return own.has("icon") ? images.get(own["icon"], size) : Ref<Texture2D>();
		}
		return Ref<Texture2D>();
	}
	return images.get(InputDeviceFamily::image_family(InputDeviceFamily::Family(family)) + "/" + control, size);
}

Ref<Texture2D> InputBindings::get_prompt(const StringName &p_action, int p_player, int p_size) {
	const Dictionary binding = get_prompt_binding(p_action, p_player);
	return binding.is_empty() ? Ref<Texture2D>() : get_binding_prompt(binding, p_player, p_size);
}

// Text with each {action} shown as its prompt (or in words where it has no image).
void InputBindings::show_prompts_in(RichTextLabel *p_label, const String &p_text, int p_player, int p_size) {
	ERR_FAIL_NULL(p_label);
	p_label->clear();
	const int height = p_size > 0 ? p_size : p_label->get_theme_font_size(SNAME("normal_font_size"));
	int at = 0;
	while (at < p_text.length()) {
		const int open = p_text.find("{", at);
		const int close = open >= 0 ? p_text.find("}", open) : -1;
		if (open < 0 || close < 0) {
			p_label->add_text(p_text.substr(at));
			break;
		}
		p_label->add_text(p_text.substr(at, open - at));
		const StringName action = p_text.substr(open + 1, close - open - 1);
		if (!actions.has(action)) {
			p_label->add_text(p_text.substr(open, close - open + 1));
		} else {
			const Ref<Texture2D> image = get_prompt(action, p_player, height * 2);
			if (image.is_valid()) {
				p_label->add_image(image, 0, height);
			} else {
				p_label->add_text(describe(action, p_player));
			}
		}
		at = close + 1;
	}
}

void InputBindings::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_actions"), &InputBindings::get_actions);
	ClassDB::bind_method(D_METHOD("get_bindings", "action", "player", "context"), &InputBindings::get_bindings, DEFVAL(0), DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("set_bindings", "action", "bindings", "player", "context"), &InputBindings::set_bindings, DEFVAL(0), DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("bind", "action", "binding", "player", "context"), &InputBindings::bind, DEFVAL(0), DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("unbind", "action", "binding", "player", "context"), &InputBindings::unbind, DEFVAL(0), DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("get_active_bindings", "action", "player"), &InputBindings::get_active_bindings, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("export_bindings", "player"), &InputBindings::export_bindings, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("import_bindings", "bindings", "player"), &InputBindings::import_bindings, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_binding_events", "binding", "player"), &InputBindings::get_binding_events, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("is_axis_heard", "device", "axis"), &InputBindings::is_axis_heard);
	ClassDB::bind_method(D_METHOD("reset", "player"), &InputBindings::reset, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("is_customised", "player"), &InputBindings::is_customised, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_actions_using", "binding", "player"), &InputBindings::get_actions_using, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_events", "action", "player"), &InputBindings::get_events, DEFVAL(0));

	ClassDB::bind_method(D_METHOD("set_save_folder", "folder"), &InputBindings::set_save_folder);
	ClassDB::bind_method(D_METHOD("get_save_folder"), &InputBindings::get_save_folder);
	ClassDB::bind_method(D_METHOD("set_context_active", "context", "active"), &InputBindings::set_context_active);
	ClassDB::bind_method(D_METHOD("is_context_active", "context"), &InputBindings::is_context_active);

	ClassDB::bind_method(D_METHOD("set_player_profile", "player", "profile"), &InputBindings::set_player_profile);
	ClassDB::bind_method(D_METHOD("get_player_profile", "player"), &InputBindings::get_player_profile);
	ClassDB::bind_method(D_METHOD("set_player_devices", "player", "devices"), &InputBindings::set_player_devices);
	ClassDB::bind_method(D_METHOD("get_player_devices", "player"), &InputBindings::get_player_devices);
	ClassDB::bind_method(D_METHOD("get_device_player", "device"), &InputBindings::get_device_player);
	ClassDB::bind_method(D_METHOD("get_player_last_device", "player"), &InputBindings::get_player_last_device);
	ClassDB::bind_method(D_METHOD("get_player_family", "player"), &InputBindings::get_player_family);

	ClassDB::bind_method(D_METHOD("get_device_family", "device"), &InputBindings::get_device_family);
	ClassDB::bind_method(D_METHOD("get_device_name", "device"), &InputBindings::get_device_name);
	ClassDB::bind_method(D_METHOD("is_steering_wheel", "device"), &InputBindings::is_steering_wheel);
	ClassDB::bind_method(D_METHOD("get_binding_devices", "binding", "player"), &InputBindings::get_binding_devices, DEFVAL(0));

	ClassDB::bind_method(D_METHOD("get_value", "action", "player"), &InputBindings::get_value, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("is_pressed", "action", "player"), &InputBindings::is_pressed, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("is_just_pressed", "action", "player"), &InputBindings::is_just_pressed, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("is_just_released", "action", "player"), &InputBindings::is_just_released, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_axis", "negative_action", "positive_action", "player"), &InputBindings::get_axis, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_vector", "negative_x", "positive_x", "negative_y", "positive_y", "player"), &InputBindings::get_vector, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("has_axis", "action", "player"), &InputBindings::has_axis, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("is_digital", "action", "player"), &InputBindings::is_digital, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_binding_value", "binding", "player"), &InputBindings::get_binding_value, DEFVAL(0));

	ClassDB::bind_method(D_METHOD("capture", "player"), &InputBindings::capture, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("cancel_capture"), &InputBindings::cancel_capture);
	ClassDB::bind_method(D_METHOD("is_capturing"), &InputBindings::is_capturing);

	ClassDB::bind_method(D_METHOD("describe_binding", "binding"), &InputBindings::describe_binding);
	ClassDB::bind_method(D_METHOD("describe", "action", "player"), &InputBindings::describe, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_binding_prompt", "binding", "player", "size"), &InputBindings::get_binding_prompt, DEFVAL(0), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_prompt", "action", "player", "size"), &InputBindings::get_prompt, DEFVAL(0), DEFVAL(0));
	ClassDB::bind_method(D_METHOD("get_prompt_binding", "action", "player"), &InputBindings::get_prompt_binding, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("show_prompts_in", "label", "text", "player", "size"), &InputBindings::show_prompts_in, DEFVAL(0), DEFVAL(0));

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "save_folder", PROPERTY_HINT_DIR, "", PROPERTY_USAGE_NONE), "set_save_folder", "get_save_folder");

	ADD_SIGNAL(MethodInfo("bindings_changed", PropertyInfo(Variant::INT, "player")));
	ADD_SIGNAL(MethodInfo("devices_changed"));
	ADD_SIGNAL(MethodInfo("player_family_changed", PropertyInfo(Variant::INT, "player"), PropertyInfo(Variant::INT, "family", PROPERTY_HINT_ENUM, "", PROPERTY_USAGE_CLASS_IS_ENUM, "InputBindings.DeviceFamily")));

	BIND_CONSTANT(KEYBOARD);
	BIND_ENUM_CONSTANT(FAMILY_KEYBOARD);
	BIND_ENUM_CONSTANT(FAMILY_XBOX);
	BIND_ENUM_CONSTANT(FAMILY_PLAYSTATION_4);
	BIND_ENUM_CONSTANT(FAMILY_PLAYSTATION_5);
	BIND_ENUM_CONSTANT(FAMILY_NINTENDO);
	BIND_ENUM_CONSTANT(FAMILY_STEAM_DECK);
	BIND_ENUM_CONSTANT(FAMILY_STEERING_WHEEL);
	BIND_ENUM_CONSTANT(FAMILY_GENERIC);
}
