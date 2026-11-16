#include "vr.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"
#include "servers/display/display_server.h"
#include "servers/xr/xr_interface.h"
#include "servers/xr/xr_server.h"

#ifdef WINDOWS_ENABLED
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

VR *VR::singleton = nullptr;

namespace {

#if defined(MACOS_ENABLED) || defined(WINDOWS_ENABLED) || defined(LINUXBSD_ENABLED)
// Where the player chooses: on a computer. Elsewhere a build is for a headset or it isn't, and
// OpenXR starts as the project says.
constexpr bool DESKTOP = true;
#else
constexpr bool DESKTOP = false;
#endif

// Set when this process uses Purr's runtime.
bool runtime_in_use = false;
// What the launch asked for: -1 nothing (the player's choice decides), 0 off, 1 on.
int launch_choice = -1;

#ifdef MACOS_ENABLED
const char *RUNTIME_LIBRARY = "libvr_runtime.dylib";
#endif
// The player's choices; the runtime and the streamer read them from the data folder too.
const char *CHOICES_FILE = "stream.json";

bool project_has_vr() {
	return ProjectSettings::get_singleton()->get_setting("xr/openxr/enabled", false); // (defined by the openxr module, later)
}

// Whether the player has an OpenXR runtime of their own (SteamVR, Meta's and others), where
// OpenXR's loader looks for the active one.
bool player_has_runtime() {
	if (!OS::get_singleton()->get_environment("XR_RUNTIME_JSON").is_empty()) {
		return true;
	}
#if defined(WINDOWS_ENABLED)
	DWORD size = 0;
	return RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime", RRF_RT_REG_SZ, nullptr, nullptr, &size) == ERROR_SUCCESS && size > 2;
#elif defined(LINUXBSD_ENABLED)
	OS *os = OS::get_singleton();
	Vector<String> folders;
	const String config_home = os->get_environment("XDG_CONFIG_HOME");
	folders.push_back(config_home.is_empty() ? os->get_environment("HOME").path_join(".config") : config_home);
	const String config_dirs = os->get_environment("XDG_CONFIG_DIRS");
	for (const String &folder : (config_dirs.is_empty() ? String("/etc/xdg") : config_dirs).split(":", false)) {
		folders.push_back(folder);
	}
	folders.push_back("/etc");
	for (const String &folder : folders) {
		if (FileAccess::exists(folder.path_join("openxr/1/active_runtime.json"))) {
			return true;
		}
	}
	return false;
#else
	return false; // On a Mac, no headset maker ships one.
#endif
}

// What the launch asks for: -openxr, -novr (which wins), or Godot's --xr-mode on or off.
int read_launch_choice() {
	List<String> arguments = OS::get_singleton()->get_cmdline_args();
	for (const String &argument : OS::get_singleton()->get_cmdline_user_args()) {
		arguments.push_back(argument);
	}
	if (arguments.find("-novr")) {
		return 0;
	}
	if (arguments.find("-openxr")) {
		return 1;
	}
	switch (XRServer::get_xr_mode()) {
		case XRServer::XRMODE_ON:
			return 1;
		case XRServer::XRMODE_OFF:
			return 0;
		default:
			return -1;
	}
}

#ifdef MACOS_ENABLED
// Points OpenXR at Purr's runtime beside the game, when the game ships it.
void use_purr_runtime(const String &p_data) {
	if (!OS::get_singleton()->get_environment("XR_RUNTIME_JSON").is_empty()) {
		return; // The player's own choice of runtime.
	}
	const String library = OS::get_singleton()->get_executable_path().get_base_dir().path_join(RUNTIME_LIBRARY);
	if (!FileAccess::exists(library)) {
		// A game without VR ships no runtime: only worth a word when it has VR.
		if (project_has_vr()) {
			WARN_PRINT(vformat("Purr's VR runtime is missing (%s), so VR isn't available.", library));
		}
		return;
	}
	DirAccess::make_dir_recursive_absolute(p_data);
	Dictionary runtime;
	runtime["name"] = "VR streamer";
	runtime["library_path"] = library;
	Dictionary manifest;
	manifest["file_format_version"] = "1.0.0";
	manifest["runtime"] = runtime;
	const String manifest_path = p_data.path_join("openxr_runtime.json");
	Ref<FileAccess> file = FileAccess::open(manifest_path, FileAccess::WRITE);
	ERR_FAIL_COND_MSG(file.is_null(), "Couldn't write the VR runtime's manifest to " + manifest_path + ".");
	file->store_string(JSON::stringify(manifest, "\t"));
	file.unref();

	OS::get_singleton()->set_environment("XR_RUNTIME_JSON", manifest_path);
	OS::get_singleton()->set_environment("PURR_VR_DATA", p_data);
	OS::get_singleton()->set_environment("PURR_VR_LOG", OS::get_singleton()->get_user_data_dir().path_join("logs").path_join("vr.log"));
	// The headset's controllers, for games that use them (most do; a game played with a pad or
	// wheel can turn them off).
	const bool controllers = GLOBAL_DEF(PropertyInfo(Variant::BOOL, "xr/controllers"), true);
	OS::get_singleton()->set_environment("PURR_VR_CONTROLLERS", controllers ? "1" : "0");
	runtime_in_use = true;
}
#endif

} // namespace

String VR::data_dir() {
	return OS::get_singleton()->get_user_data_dir().path_join("vr");
}

void VR::start_up() {
	if (Engine::get_singleton()->is_editor_hint() || Engine::get_singleton()->is_project_manager_hint()) {
		return;
	}
	launch_choice = read_launch_choice();
#ifdef MACOS_ENABLED
	use_purr_runtime(data_dir());
#endif
	if (!DESKTOP) {
		return;
	}
	// OpenXR reads this as it starts: the launch's choice, else the player's (off until they
	// choose), for a game with VR.
	if (launch_choice >= 0) {
		XRServer::set_xr_mode(launch_choice == 1 ? XRServer::XRMODE_ON : XRServer::XRMODE_OFF);
	} else if (project_has_vr()) {
		const bool chosen = read_choices().get("enabled", false);
		XRServer::set_xr_mode(chosen ? XRServer::XRMODE_ON : XRServer::XRMODE_OFF);
	}
}

bool VR::is_available() const {
	if (!project_has_vr()) {
		return false;
	}
	if (!DESKTOP) {
		return true;
	}
#ifdef MACOS_ENABLED
	return runtime_in_use || player_has_runtime();
#else
	return player_has_runtime();
#endif
}

bool VR::can_choose() const {
	return DESKTOP && launch_choice < 0 && is_available();
}

bool VR::is_running() const {
	XRServer *xr = XRServer::get_singleton();
	const Ref<XRInterface> openxr = xr ? xr->find_interface("OpenXR") : Ref<XRInterface>();
	return openxr.is_valid() && openxr->is_initialized();
}

void VR::restart() {
	OS::get_singleton()->set_restart_on_exit(true, OS::get_singleton()->get_cmdline_args());
	SceneTree *tree = SceneTree::get_singleton();
	ERR_FAIL_NULL(tree);
	tree->quit();
}

// The player's choices.

Dictionary VR::read_choices() {
	const String path = data_dir().path_join(CHOICES_FILE);
	if (!FileAccess::exists(path)) {
		return Dictionary(); // Nothing chosen yet: the defaults.
	}
	const Variant parsed = JSON::parse_string(FileAccess::get_file_as_string(path));
	return parsed.get_type() == Variant::DICTIONARY ? Dictionary(parsed) : Dictionary();
}

void VR::save_choices() {
	DirAccess::make_dir_recursive_absolute(data_dir());
	Ref<FileAccess> file = FileAccess::open(data_dir().path_join(CHOICES_FILE), FileAccess::WRITE);
	ERR_FAIL_COND_MSG(file.is_null(), "Couldn't save the VR choices.");
	file->store_string(JSON::stringify(choices, "\t"));
}

Variant VR::get_choice(const String &p_key, const Variant &p_default) const {
	return choices.get(p_key, p_default);
}

void VR::set_choice(const String &p_key, const Variant &p_value) {
	if (choices.has(p_key) && choices[p_key] == p_value) {
		return; // Games set their choices whenever their settings apply: only a change is saved.
	}
	choices[p_key] = p_value;
	save_choices();
}

void VR::set_enabled(bool p_enabled) {
	set_choice("enabled", p_enabled);
}

bool VR::is_enabled() const {
	return get_choice("enabled", false);
}

void VR::set_eye_scale(double p_scale) {
	ERR_FAIL_COND_MSG(p_scale < 0.5 || p_scale > 1.0, "The eye scale is from 0.5 to 1.0.");
	set_choice("eye_scale", p_scale);
}

double VR::get_eye_scale() const {
	return get_choice("eye_scale", 1.0);
}

void VR::set_refresh_rate(int p_hz) {
	ERR_FAIL_COND_MSG(p_hz != 72 && p_hz != 80 && p_hz != 90 && p_hz != 120, "The refresh rate is 72, 80, 90 or 120 Hz.");
	set_choice("hz", p_hz);
}

int VR::get_refresh_rate() const {
	return get_choice("hz", 72);
}

void VR::set_bitrate(int p_mbps) {
	ERR_FAIL_COND_MSG(p_mbps < 5 || p_mbps > 200, "The bitrate is from 5 to 200 Mbps.");
	set_choice("bitrate", p_mbps);
}

int VR::get_bitrate() const {
	return get_choice("bitrate", 100);
}

void VR::set_mute_computer(bool p_mute) {
	set_choice("mute", p_mute);
}

bool VR::get_mute_computer() const {
	return get_choice("mute", true);
}

void VR::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_available"), &VR::is_available);
	ClassDB::bind_method(D_METHOD("can_choose"), &VR::can_choose);
	ClassDB::bind_method(D_METHOD("is_running"), &VR::is_running);
	ClassDB::bind_method(D_METHOD("restart"), &VR::restart);
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &VR::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &VR::is_enabled);
	ClassDB::bind_method(D_METHOD("set_eye_scale", "scale"), &VR::set_eye_scale);
	ClassDB::bind_method(D_METHOD("get_eye_scale"), &VR::get_eye_scale);
	ClassDB::bind_method(D_METHOD("set_refresh_rate", "hz"), &VR::set_refresh_rate);
	ClassDB::bind_method(D_METHOD("get_refresh_rate"), &VR::get_refresh_rate);
	ClassDB::bind_method(D_METHOD("set_bitrate", "mbps"), &VR::set_bitrate);
	ClassDB::bind_method(D_METHOD("get_bitrate"), &VR::get_bitrate);
	ClassDB::bind_method(D_METHOD("set_mute_computer", "mute"), &VR::set_mute_computer);
	ClassDB::bind_method(D_METHOD("get_mute_computer"), &VR::get_mute_computer);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "eye_scale", PROPERTY_HINT_RANGE, "0.5,1.0,0.05"), "set_eye_scale", "get_eye_scale");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "refresh_rate", PROPERTY_HINT_ENUM, "72:72,80:80,90:90,120:120"), "set_refresh_rate", "get_refresh_rate");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bitrate", PROPERTY_HINT_RANGE, "5,200"), "set_bitrate", "get_bitrate");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "mute_computer"), "set_mute_computer", "get_mute_computer");
}

VR::VR() {
	singleton = this;
	choices = read_choices();
	if (runtime_in_use) {
		callable_mp(this, &VR::watch_xr).call_deferred();
	}
}

// OpenXR's interface, once the engine has made it: its sessions say when VR runs.
void VR::watch_xr() {
	XRServer *xr = XRServer::get_singleton();
	const Ref<XRInterface> openxr = xr ? xr->find_interface("OpenXR") : Ref<XRInterface>();
	if (openxr.is_valid()) {
		openxr->connect(SNAME("session_begun"), callable_mp(this, &VR::xr_session_begun));
		openxr->connect(SNAME("session_stopping"), callable_mp(this, &VR::xr_session_stopping));
		// A game that starts in VR has begun its session by now.
		if (openxr->is_initialized()) {
			xr_session_begun();
		}
	}
}

void VR::xr_session_begun() {
	DisplayServer *display = DisplayServer::get_singleton();
	if (!display || vsync_before_vr >= 0) {
		return;
	}
	vsync_before_vr = display->window_get_vsync_mode(DisplayServerEnums::MAIN_WINDOW_ID);
	display->window_set_vsync_mode(DisplayServerEnums::VSYNC_DISABLED);
}

void VR::xr_session_stopping() {
	DisplayServer *display = DisplayServer::get_singleton();
	if (display && vsync_before_vr >= 0) {
		display->window_set_vsync_mode(DisplayServerEnums::VSyncMode(vsync_before_vr));
	}
	vsync_before_vr = -1;
}

VR::~VR() {
	singleton = nullptr;
}
