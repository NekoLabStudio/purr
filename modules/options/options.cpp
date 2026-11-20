#include "options.h"

#include "options_watcher.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "servers/audio/audio_server.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server.h"
#include "servers/xr/xr_server.h"

#include "modules/graphics/recommended_graphics.h"

Options *Options::singleton = nullptr;

namespace {
const char *WINDOW_MODE = "display/window_mode";
const char *VSYNC = "display/vsync";
const char *MAX_FPS = "display/max_fps";
const char *GRAPHICS_TIER = "display/graphics_tier";
// Whether the player agreed to send crash reports to the game's online service (CrashReports).
const char *SEND_CRASH_REPORTS = "privacy/send_crash_reports";
const char *TIERS[] = { "ultra", "high", "medium", "low" }; // As RecommendedGraphics::Tier counts.
const char *AUDIO_PREFIX = "audio/";
const char *VOLUME_SUFFIX = "_volume";
const char *PROJECT_PREFIX = "options/";
constexpr uint64_t WRITE_INTERVAL_USEC = 1000000;

// The project's own window mode and vsync, as Project Settings had them before the player's
// choices replaced them.
int project_window_mode = DisplayServerEnums::WINDOW_MODE_WINDOWED;
int project_vsync_mode = DisplayServerEnums::VSYNC_ENABLED;
int project_max_fps = 0;

bool is_game() {
	return !Engine::get_singleton()->is_editor_hint() && !Engine::get_singleton()->is_project_manager_hint();
}

String window_mode_name(int p_mode) {
	switch (p_mode) {
		case DisplayServerEnums::WINDOW_MODE_FULLSCREEN:
			return "fullscreen";
		case DisplayServerEnums::WINDOW_MODE_EXCLUSIVE_FULLSCREEN:
			return "exclusive_fullscreen";
		default:
			return "windowed";
	}
}

// A choice that's the project's default gives back the project's own mode, which may be maximised.
int window_mode_value(const String &p_name) {
	if (p_name == window_mode_name(project_window_mode)) {
		return project_window_mode;
	}
	if (p_name == "fullscreen") {
		return DisplayServerEnums::WINDOW_MODE_FULLSCREEN;
	}
	if (p_name == "exclusive_fullscreen") {
		return DisplayServerEnums::WINDOW_MODE_EXCLUSIVE_FULLSCREEN;
	}
	return DisplayServerEnums::WINDOW_MODE_WINDOWED;
}

int vsync_mode_value(bool p_on) {
	if (!p_on) {
		return DisplayServerEnums::VSYNC_DISABLED;
	}
	return project_vsync_mode == DisplayServerEnums::VSYNC_DISABLED ? int(DisplayServerEnums::VSYNC_ENABLED) : project_vsync_mode;
}
} // namespace

String Options::_path() {
	return "user://options.cfg";
}

// The saved values by key. A file that can't be read is set aside, so the game starts with the
// defaults and the next save doesn't lose what a person might still recover from it.
Dictionary Options::_read_file() {
	Dictionary values;
	if (!FileAccess::exists(_path())) {
		return values;
	}
	Ref<ConfigFile> file;
	file.instantiate();
	if (file->load(_path()) != OK) {
		const String aside = _path().get_basename() + ".bad.cfg";
		WARN_PRINT(vformat("The player's options couldn't be read, so the defaults are used. The file is kept as %s.", aside));
		DirAccess::rename_absolute(_path(), aside);
		return values;
	}
	for (const String &section : file->get_sections()) {
		for (const String &key : file->get_section_keys(section)) {
			values[section + "/" + key] = file->get_value(section, key);
		}
	}
	return values;
}

void Options::apply_before_window() {
	project_window_mode = GLOBAL_GET("display/window/size/mode");
	// Defined by the engine only as it opens the window, after this.
	project_vsync_mode = ProjectSettings::get_singleton()->get_setting("display/window/vsync/vsync_mode", DisplayServerEnums::VSYNC_ENABLED);
	project_max_fps = GLOBAL_GET("application/run/max_fps");
	if (!is_game()) {
		return;
	}
	const Dictionary saved = _read_file();
	ProjectSettings *settings = ProjectSettings::get_singleton();
	// Only in memory: the engine reads these as it opens the window.
	if (saved.get(WINDOW_MODE, Variant()).get_type() == Variant::STRING) {
		settings->set_setting("display/window/size/mode", window_mode_value(saved[WINDOW_MODE]));
	}
	if (saved.get(VSYNC, Variant()).get_type() == Variant::BOOL) {
		settings->set_setting("display/window/vsync/vsync_mode", vsync_mode_value(saved[VSYNC]));
	}
	if (saved.get(MAX_FPS, Variant()).get_type() == Variant::INT) {
		Engine::get_singleton()->set_max_fps(CLAMP(int(saved[MAX_FPS]), 0, 1000));
	}
}

Options::Options() {
	singleton = this;
	// In the editor the options are there to read, but nothing is saved or put into effect.
	if (is_game()) {
		saved = _read_file();
	}
	apply_graphics_tier = GLOBAL_DEF("rendering/graphics_tier/apply", true);
	_define_built_ins();
	_define_from_project();
	if (is_game()) {
		_apply_graphics_tier(true);
	}
	if (is_game()) {
		callable_mp(this, &Options::_start).call_deferred();
	}
}

// Once the game has started: the audio buses are known by then, and the scene tree is there.
void Options::_start() {
	_ensure_audio();
	SceneTree *tree = SceneTree::get_singleton();
	if (tree != nullptr) {
		OptionsWatcher *watcher = memnew(OptionsWatcher);
		watcher->set_process_mode(Node::PROCESS_MODE_ALWAYS);
		tree->get_root()->call_deferred(SNAME("add_child"), watcher, false, Node::INTERNAL_MODE_BACK);
	}
}

Options::~Options() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

void Options::_define_built_ins() {
	Dictionary window_info;
	window_info["choices"] = PackedStringArray({ "windowed", "fullscreen", "exclusive_fullscreen" });
	define(WINDOW_MODE, window_mode_name(project_window_mode), window_info);
	define(VSYNC, project_vsync_mode != DisplayServerEnums::VSYNC_DISABLED);
	Dictionary fps_info;
	fps_info["min"] = 0;
	fps_info["max"] = 1000;
	define(MAX_FPS, project_max_fps, fps_info);

	define(SEND_CRASH_REPORTS, false);

	RecommendedGraphics *graphics = RecommendedGraphics::get_singleton();
	if (graphics != nullptr) {
		Dictionary tier_info;
		tier_info["choices"] = PackedStringArray({ TIERS[0], TIERS[1], TIERS[2], TIERS[3] });
		define(GRAPHICS_TIER, TIERS[graphics->get_tier()], tier_info);
	}
}

// The window may go fullscreen or back another way (the window's own button, the game's code): the
// option says what it is now, without putting it into effect again. OptionsWatcher asks a few times
// a second, as the window can't be relied on to say so once the change has finished.
void Options::window_mode_check() {
	DisplayServer *display = DisplayServer::get_singleton();
	if (display == nullptr) {
		return;
	}
	const DisplayServerEnums::WindowMode mode = display->window_get_mode();
	const String current = get_value(WINDOW_MODE);
	String now;
	if (mode == DisplayServerEnums::WINDOW_MODE_FULLSCREEN || mode == DisplayServerEnums::WINDOW_MODE_EXCLUSIVE_FULLSCREEN) {
		now = window_mode_name(mode);
	} else if (mode != DisplayServerEnums::WINDOW_MODE_MINIMIZED && current != "windowed") {
		now = "windowed";
	}
	if (now.is_empty() || now == current) {
		return;
	}
	if (now == String(options[WINDOW_MODE].default_value)) {
		saved.erase(WINDOW_MODE);
	} else {
		saved[WINDOW_MODE] = now;
	}
	_queue_write();
	emit_signal(SNAME("changed"), WINDOW_MODE, now);
}

// The tier's settings: as Project Settings for viewports still to come (the game's own window
// among them, as the game starts), and on the window when the player changes it.
void Options::_apply_graphics_tier(bool p_starting) {
	RecommendedGraphics *graphics = RecommendedGraphics::get_singleton();
	if (graphics == nullptr || !apply_graphics_tier) {
		return;
	}
	const String name = get_value(GRAPHICS_TIER);
	int tier = 0;
	while (tier < 3 && name != TIERS[tier]) {
		tier++;
	}
	// In a headset, the tier's headset settings (no TAA, which smears as the head turns): VR decides
	// as the engine starts whether OpenXR runs, before this.
	const XRServer::XRMode xr_mode = XRServer::get_xr_mode();
	const bool headset = xr_mode == XRServer::XRMODE_ON || (xr_mode == XRServer::XRMODE_DEFAULT && bool(GLOBAL_GET("xr/openxr/enabled")));
	const Dictionary settings = graphics->get_settings(RecommendedGraphics::Tier(tier), headset);
	static const char *SETTINGS[][2] = {
		{ "scaling_3d_mode", "rendering/scaling_3d/mode" },
		{ "scaling_3d_scale", "rendering/scaling_3d/scale" },
		{ "msaa_3d", "rendering/anti_aliasing/quality/msaa_3d" },
		{ "screen_space_aa", "rendering/anti_aliasing/quality/screen_space_aa" },
		{ "use_taa", "rendering/anti_aliasing/quality/use_taa" },
		{ "anisotropic_filtering_level", "rendering/textures/default_filters/anisotropic_filtering_level" },
		{ "mesh_lod_threshold", "rendering/mesh_lod/lod_change/threshold_pixels" },
		{ "directional_shadow_size", "rendering/lights_and_shadows/directional_shadow/size" },
		{ "contact_shadows", "rendering/lights_and_shadows/contact_shadow/enabled" },
		{ "skip_mipmaps", "rendering/textures/quality/skip_mipmaps" },
	};
	ProjectSettings *project = ProjectSettings::get_singleton();
	for (const auto &setting : SETTINGS) {
		if (settings.has(setting[0])) {
			project->set_setting(setting[1], settings[setting[0]]);
		}
	}
	SceneTree *tree = SceneTree::get_singleton();
	if (p_starting || tree == nullptr || tree->get_root() == nullptr) {
		// The renderer read the shadow size as it started; the window reads the rest as it opens.
		if (settings.has("directional_shadow_size") && RenderingServer::get_singleton() != nullptr) {
			RenderingServer::get_singleton()->directional_shadow_atlas_set_size(settings["directional_shadow_size"], true);
		}
		return;
	}
	graphics->apply(tree->get_root(), settings);
}

// Settings under options/ in Project Settings, with their range or choices if they have one.
void Options::_define_from_project() {
	List<PropertyInfo> properties;
	ProjectSettings::get_singleton()->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (!property.name.begins_with(PROJECT_PREFIX)) {
			continue;
		}
		Dictionary info;
		const PackedStringArray hint = property.hint_string.split(",");
		if (property.hint == PROPERTY_HINT_RANGE && hint.size() >= 2) {
			info["min"] = hint[0].to_float();
			info["max"] = hint[1].to_float();
		} else if (property.hint == PROPERTY_HINT_ENUM && property.type == Variant::STRING) {
			info["choices"] = hint;
		}
		define(property.name.trim_prefix(PROJECT_PREFIX), GLOBAL_GET(property.name), info);
	}
}

// A volume for each audio bus, as the bus layout sets it, named for the bus: Music is
// audio/music_volume. Defined once the layout is loaded, and again as buses are added.
void Options::_ensure_audio() {
	AudioServer *audio = AudioServer::get_singleton();
	if (!is_game() || audio == nullptr) {
		return;
	}
	if (!audio_defined) {
		audio_defined = true;
		audio->connect("bus_layout_changed", callable_mp(this, &Options::_ensure_audio));
	}
	Dictionary info;
	info["min"] = 0.0;
	info["max"] = 1.0;
	for (int i = 0; i < audio->get_bus_count(); i++) {
		const String key = AUDIO_PREFIX + audio->get_bus_name(i).to_snake_case() + VOLUME_SUFFIX;
		if (!options.has(key)) {
			define(key, double(audio->get_bus_volume_linear(i)), info);
		}
	}
}

void Options::define(const String &p_key, const Variant &p_default, const Dictionary &p_info) {
	ERR_FAIL_COND_MSG(p_key.get_slice_count("/") < 2 || p_key.begins_with("/") || p_key.ends_with("/"), vformat("The option key \"%s\" needs a section, as in \"game/show_hints\".", p_key));
	ERR_FAIL_COND_MSG(p_default.get_type() == Variant::NIL, vformat("The option \"%s\" needs a default.", p_key));
	const bool redefined = options.has(p_key);
	if (!redefined) {
		order.push_back(p_key);
	}
	Option &option = options[p_key];
	option.default_value = p_default;
	option.info = p_info.duplicate();
	// A saved value that no longer fits (another type, out of range, a choice that's gone) is dropped.
	if (saved.has(p_key)) {
		Variant value;
		if (_conform(p_key, saved[p_key], value) && value != p_default) {
			saved[p_key] = value;
		} else {
			saved.erase(p_key);
			_queue_write();
		}
	}
	// The window's options were put into effect as the engine started.
	if (saved.has(p_key) && p_key != WINDOW_MODE && p_key != VSYNC && p_key != MAX_FPS && p_key != GRAPHICS_TIER) {
		_apply(p_key);
	}
}

// Whether a value suits the option, as r_value: numbers convert between int and float and are
// clamped to the range.
bool Options::_conform(const String &p_key, const Variant &p_value, Variant &r_value) const {
	const Option &option = options[p_key];
	const Variant::Type type = option.default_value.get_type();
	const bool number = type == Variant::INT || type == Variant::FLOAT;
	if (number && (p_value.get_type() == Variant::INT || p_value.get_type() == Variant::FLOAT)) {
		double value = p_value;
		if (option.info.has("min")) {
			value = MAX(value, double(option.info["min"]));
		}
		if (option.info.has("max")) {
			value = MIN(value, double(option.info["max"]));
		}
		r_value = type == Variant::INT ? Variant(int64_t(Math::round(value))) : Variant(value);
		return true;
	}
	if (p_value.get_type() != type) {
		return false;
	}
	if (option.info.has("choices")) {
		const Array choices = option.info["choices"];
		if (!choices.has(p_value)) {
			return false;
		}
	}
	r_value = p_value;
	return true;
}

// Puts an engine option into effect; the game's own are the game's to act on.
void Options::_apply(const String &p_key) {
	if (!is_game()) {
		return;
	}
	const Variant value = get_value(p_key);
	if (p_key == MAX_FPS) {
		Engine::get_singleton()->set_max_fps(value);
		return;
	}
	if (p_key == GRAPHICS_TIER) {
		_apply_graphics_tier(false);
		return;
	}
	DisplayServer *display = DisplayServer::get_singleton();
	if (p_key == WINDOW_MODE) {
		if (display != nullptr) {
			const DisplayServerEnums::WindowMode mode = DisplayServerEnums::WindowMode(window_mode_value(value));
			if (display->window_get_mode() != mode) {
				display->window_set_mode(mode);
			}
		}
		return;
	}
	if (p_key == VSYNC) {
		if (display != nullptr) {
			display->window_set_vsync_mode(DisplayServerEnums::VSyncMode(vsync_mode_value(value)));
		}
		return;
	}
	if (p_key.begins_with(AUDIO_PREFIX) && p_key.ends_with(VOLUME_SUFFIX)) {
		AudioServer *audio = AudioServer::get_singleton();
		for (int i = 0; audio != nullptr && i < audio->get_bus_count(); i++) {
			if (AUDIO_PREFIX + audio->get_bus_name(i).to_snake_case() + VOLUME_SUFFIX == p_key) {
				audio->set_bus_volume_linear(i, value);
			}
		}
	}
}

bool Options::has_option(const String &p_key) {
	_ensure_audio();
	return options.has(p_key);
}

PackedStringArray Options::get_options() {
	_ensure_audio();
	PackedStringArray keys;
	for (const String &key : order) {
		keys.push_back(key);
	}
	return keys;
}

Variant Options::get_value(const String &p_key) {
	_ensure_audio();
	ERR_FAIL_COND_V_MSG(!options.has(p_key), Variant(), vformat("There's no option \"%s\".", p_key));
	return saved.get(p_key, options[p_key].default_value);
}

void Options::set_value(const String &p_key, const Variant &p_value) {
	_ensure_audio();
	ERR_FAIL_COND_MSG(!options.has(p_key), vformat("There's no option \"%s\".", p_key));
	Variant value;
	ERR_FAIL_COND_MSG(!_conform(p_key, p_value, value), vformat("%s doesn't suit the option \"%s\".", p_value.stringify(), p_key));
	if (value == get_value(p_key)) {
		return;
	}
	// Only what differs from the default is saved, so a game update's new default reaches players
	// who never chose.
	if (value == options[p_key].default_value) {
		saved.erase(p_key);
	} else {
		saved[p_key] = value;
	}
	_apply(p_key);
	_queue_write();
	emit_signal(SNAME("changed"), p_key, value);
}

Variant Options::get_default(const String &p_key) {
	_ensure_audio();
	ERR_FAIL_COND_V_MSG(!options.has(p_key), Variant(), vformat("There's no option \"%s\".", p_key));
	return options[p_key].default_value;
}

Dictionary Options::get_info(const String &p_key) {
	_ensure_audio();
	ERR_FAIL_COND_V_MSG(!options.has(p_key), Dictionary(), vformat("There's no option \"%s\".", p_key));
	return options[p_key].info.duplicate();
}

void Options::reset(const String &p_key) {
	set_value(p_key, get_default(p_key));
}

void Options::reset_all() {
	for (const String &key : get_options()) {
		reset(key);
	}
}

// Changes are written at most once a second, so a slider dragged across its range writes the file
// a few times rather than on every step.
void Options::_queue_write() {
	if (!is_game()) {
		return;
	}
	dirty = true;
	SceneTree *tree = SceneTree::get_singleton();
	if (tree == nullptr) {
		_write();
		return;
	}
	if (!flushing) {
		flushing = true;
		tree->connect("process_frame", callable_mp(this, &Options::_flush));
	}
}

void Options::_flush() {
	if (OS::get_singleton()->get_ticks_usec() - last_write_usec < WRITE_INTERVAL_USEC) {
		return;
	}
	_write();
	flushing = false;
	SceneTree::get_singleton()->disconnect("process_frame", callable_mp(this, &Options::_flush));
}

// Values no option has now (a later define() may still want them) are kept. The file is written
// beside the old one and moved over it, so it's never left half written.
void Options::_write() {
	if (!dirty) {
		return;
	}
	dirty = false;
	last_write_usec = OS::get_singleton()->get_ticks_usec();
	Ref<ConfigFile> file;
	file.instantiate();
	for (const KeyValue<Variant, Variant> &entry : saved) {
		const String key = entry.key;
		file->set_value(key.get_slice("/", 0), key.substr(key.find_char('/') + 1), entry.value);
	}
	const String temporary = _path() + ".tmp";
	if (file->save(temporary) != OK || DirAccess::rename_absolute(temporary, _path()) != OK) {
		ERR_PRINT("The player's options couldn't be saved.");
	}
}

// Writes what's changed now, rather than within the second.
void Options::finish() {
	_write();
}

void Options::_bind_methods() {
	ClassDB::bind_method(D_METHOD("define", "key", "default", "info"), &Options::define, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("has_option", "key"), &Options::has_option);
	ClassDB::bind_method(D_METHOD("get_options"), &Options::get_options);
	ClassDB::bind_method(D_METHOD("get_value", "key"), &Options::get_value);
	ClassDB::bind_method(D_METHOD("set_value", "key", "value"), &Options::set_value);
	ClassDB::bind_method(D_METHOD("get_default", "key"), &Options::get_default);
	ClassDB::bind_method(D_METHOD("get_info", "key"), &Options::get_info);
	ClassDB::bind_method(D_METHOD("reset", "key"), &Options::reset);
	ClassDB::bind_method(D_METHOD("reset_all"), &Options::reset_all);

	ADD_SIGNAL(MethodInfo("changed", PropertyInfo(Variant::STRING, "key"), PropertyInfo(Variant::NIL, "value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
}
