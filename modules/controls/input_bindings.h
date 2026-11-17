#pragma once

// InputBindings: the player's own controls, over Godot's InputMap. The game's defaults are its
// Input Map (and, optionally, a defaults file with calibration and contexts); each player's
// changes are saved in the user folder and loaded over them. Joypads are found by name or
// GUID, never by Godot's device number, so a binding follows its controller between sessions;
// "gamepad" means any controller (but never a steering wheel). Axes are calibrated, sets of
// bindings switch on with contexts, local players each have their devices (and can have their
// own bindings), and every binding has a prompt: an image for the controller the player is
// using, or words.
//
// Player 1's bindings are put into Godot's InputMap, so Input.is_action_pressed() and the
// rest work as usual; every player's are read through get_value(), is_pressed() and the like.

#include "input_bindings_call.h"
#include "input_device_family.h"
#include "input_prompt_images.h"

#include "core/input/input_event.h"
#include "core/object/object.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/variant/typed_array.h"

class RichTextLabel;
class Texture2D;

class InputBindings : public Object {
	GDCLASS(InputBindings, Object);

public:
	enum DeviceFamily {
		FAMILY_KEYBOARD = InputDeviceFamily::KEYBOARD,
		FAMILY_XBOX = InputDeviceFamily::XBOX,
		FAMILY_PLAYSTATION_4 = InputDeviceFamily::PLAYSTATION_4,
		FAMILY_PLAYSTATION_5 = InputDeviceFamily::PLAYSTATION_5,
		FAMILY_NINTENDO = InputDeviceFamily::NINTENDO,
		FAMILY_STEAM_DECK = InputDeviceFamily::STEAM_DECK,
		FAMILY_STEERING_WHEEL = InputDeviceFamily::STEERING_WHEEL,
		FAMILY_GENERIC = InputDeviceFamily::GENERIC,
	};

	// The keyboard and mouse, where a device number is asked for.
	static constexpr int KEYBOARD = -1;

private:
	static InputBindings *singleton;

	// The game's defaults: action -> bindings, and context -> action -> bindings.
	Dictionary defaults;
	Dictionary default_contexts;
	// Each profile's bindings (as defaults, or as the player saved them), by profile name.
	HashMap<String, Dictionary> profiles;
	HashSet<String> customised;
	HashSet<String> active_contexts;
	// The actions this manages (the game's, never Godot's own ui_*), with their deadzones.
	HashMap<StringName, float> actions;

	// When each action was last pressed and released, as a drawn frame and as a physics tick,
	// so "just pressed" holds for exactly one of either, as Input's does.
	struct Presses {
		uint64_t pressed_frame = UINT64_MAX;
		uint64_t pressed_tick = UINT64_MAX;
		uint64_t released_frame = UINT64_MAX;
		uint64_t released_tick = UINT64_MAX;
	};

	struct Player {
		String profile = "default";
		PackedInt32Array devices; // Assigned to this player; player 1 also has whatever nobody has.
		bool assigned = false;
		int last_device = KEYBOARD;
		int previous_device = KEYBOARD; // Used before the last, for when the last is given to someone else.
		HashSet<StringName> held;
		HashMap<StringName, Presses> presses;
	};
	HashMap<int, Player> players;

	// Per joypad: what it is (worked out once), and its axes heard since it was plugged in
	// (until one reports, Godot reads it as 0, which isn't where it rests).
	struct Joypad {
		bool known = false;
		bool wheel = false;
		DeviceFamily family = FAMILY_GENERIC;
		Dictionary layout;
		Ref<RefCounted> hats; // A SteeringWheelHats reading its hat, or null.
		bool hats_checked = false;
		bool own_buttons = false; // Its own buttons read by hats (SDL renumbered them).
		HashSet<int> own_held;
		int own_hat = 0;
		HashSet<int> from_hat; // Godot's D-pad buttons held by the hat, not the wheel's own.
		HashSet<int> heard;
	};
	HashMap<int, Joypad> joypads;

	// Godot's own ui_* joypad events, taken off and put back for each controller (not wheels).
	HashMap<StringName, Vector<Ref<InputEvent>>> ui_defaults;
	Vector<Pair<StringName, Ref<InputEvent>>> ui_added;

	Ref<InputBindingsCall> capture_call;
	int capture_player = 0;
	HashMap<Vector2i, float> capture_rest;

	String save_folder = "user://input_bindings";
	String profile_path(const String &p_name) const;

	InputPromptImages images;
	bool started = false;

	void start();
	void read_defaults();
	Dictionary &profile_of(int p_player);
	void load_profile(const String &p_name);
	void save_profile(const String &p_name);
	void rebuild();
	void put_back_ui_events();

	Player &player(int p_player);
	Joypad &joypad(int p_device);
	bool player_has(int p_player, int p_device) const;
	bool first_player_assigned() const;
	void use_device(int p_player, int p_device);
	void settle_last_devices();
	Array active_bindings(const StringName &p_action, int p_player);
	PackedInt32Array devices_of(const Dictionary &p_binding, int p_player);
	float read(const Dictionary &p_binding, int p_player);
	float pressed_by_game(const StringName &p_action);
	Vector<Ref<InputEvent>> events_of(const Dictionary &p_binding, int p_player);
	int hat_of(int p_device);
	void feed_own_buttons(int p_device);
	void tell_hat_apart(const Ref<InputEvent> &p_event);
	void finish_capture(const Dictionary &p_result);
	// Mouse wheel turns seen since the last frame (a turn is a press let go at once).
	HashSet<int> wheel_turns;
	PackedInt32Array find_devices(const Dictionary &p_binding);
	Dictionary binding_of_event(const Ref<InputEvent> &p_event, int p_device);
	Dictionary device_fields(int p_device);
	bool reads_as_gamepad(int p_device);

	void joy_connection_changed(int p_device, bool p_connected);
	void frame();
	void track_presses();
	bool happened_now(uint64_t p_frame, uint64_t p_tick) const;

protected:
	static void _bind_methods();

public:
	static InputBindings *get_singleton() { return singleton; }

	// For the listener node, which sees every input event first: whether it was taken here (a
	// capture), so the game doesn't also act on it.
	bool input(const Ref<InputEvent> &p_event);

	PackedStringArray get_actions();
	TypedArray<Dictionary> get_bindings(const StringName &p_action, int p_player = 0, const String &p_context = String());
	void set_bindings(const StringName &p_action, const TypedArray<Dictionary> &p_bindings, int p_player = 0, const String &p_context = String());
	void bind(const StringName &p_action, const Dictionary &p_binding, int p_player = 0, const String &p_context = String());
	void unbind(const StringName &p_action, const Dictionary &p_binding, int p_player = 0, const String &p_context = String());
	TypedArray<Dictionary> get_active_bindings(const StringName &p_action, int p_player = 0);
	Dictionary export_bindings(int p_player = 0);
	void import_bindings(const Dictionary &p_bindings, int p_player = 0);
	TypedArray<InputEvent> get_binding_events(const Dictionary &p_binding, int p_player = 0);
	bool is_axis_heard(int p_device, int p_axis);
	void reset(int p_player = 0);
	bool is_customised(int p_player = 0);
	PackedStringArray get_actions_using(const Dictionary &p_binding, int p_player = 0);
	TypedArray<InputEvent> get_events(const StringName &p_action, int p_player = 0);

	void set_save_folder(const String &p_folder);
	String get_save_folder() const { return save_folder; }

	void set_context_active(const String &p_context, bool p_active);
	bool is_context_active(const String &p_context) const;

	void set_player_profile(int p_player, const String &p_profile);
	String get_player_profile(int p_player);
	void set_player_devices(int p_player, const PackedInt32Array &p_devices);
	PackedInt32Array get_player_devices(int p_player);
	int get_device_player(int p_device);
	int get_player_last_device(int p_player);
	DeviceFamily get_player_family(int p_player);

	DeviceFamily get_device_family(int p_device);
	String get_device_name(int p_device);
	bool is_steering_wheel(int p_device);
	PackedInt32Array get_binding_devices(const Dictionary &p_binding, int p_player = 0);

	float get_value(const StringName &p_action, int p_player = 0);
	bool is_pressed(const StringName &p_action, int p_player = 0);
	bool is_just_pressed(const StringName &p_action, int p_player = 0);
	bool is_just_released(const StringName &p_action, int p_player = 0);
	float get_axis(const StringName &p_negative, const StringName &p_positive, int p_player = 0);
	Vector2 get_vector(const StringName &p_negative_x, const StringName &p_positive_x, const StringName &p_negative_y, const StringName &p_positive_y, int p_player = 0);
	bool has_axis(const StringName &p_action, int p_player = 0);
	bool is_digital(const StringName &p_action, int p_player = 0);
	float get_binding_value(const Dictionary &p_binding, int p_player = 0);

	Signal capture(int p_player = 0);
	void cancel_capture();
	bool is_capturing() const { return capture_call.is_valid(); }

	String describe_binding(const Dictionary &p_binding);
	String describe(const StringName &p_action, int p_player = 0);
	Ref<Texture2D> get_binding_prompt(const Dictionary &p_binding, int p_player = 0, int p_size = 0);
	Ref<Texture2D> get_prompt(const StringName &p_action, int p_player = 0, int p_size = 0);
	Dictionary get_prompt_binding(const StringName &p_action, int p_player = 0);
	void show_prompts_in(RichTextLabel *p_label, const String &p_text, int p_player = 0, int p_size = 0);

	InputBindings();
	~InputBindings();
};

VARIANT_ENUM_CAST(InputBindings::DeviceFamily);
