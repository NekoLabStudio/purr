#include "purr_export_plugin.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/version.h"
#include "editor/export/editor_export_preset.h"
#include "editor/file_system/editor_paths.h"
#include "modules/modules_enabled.gen.h" // For godotsteam.

namespace {

// Where the installed export templates for this version are, with what Purr adds to exports.
String templates_dir() {
	return EditorPaths::get_singleton()->get_export_templates_dir().path_join(GODOT_VERSION_FULL_CONFIG);
}

const char *PLAY_GAMES_LIBRARY = "purr_play_games.aar";

String play_games_app_id() {
	return String(GLOBAL_GET("play_games/app_id")).strip_edges();
}

} // namespace

void PurrExportPlugin::_export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) {
	const bool apple = p_features.has("macos") || p_features.has("ios") || p_features.has("tvos") || p_features.has("visionos");
	const String icloud_container = String(ProjectSettings::get_singleton()->get_setting("icloud/container", "")).strip_edges();

	// tvOS may delete a game's local files whenever it needs space, so saves with no copy elsewhere
	// go in its preferences, which it keeps but limits to about 500 KB for the whole game.
	if (p_features.has("tvos")) {
		const String backend = GLOBAL_GET("saves/backend");
		if (backend == "local" || (backend == "automatic" && icloud_container.is_empty())) {
			WARN_PRINT("On tvOS, saves kept only on the device share about 500 KB of storage for the whole game (tvOS deletes other local files when it needs space). Set icloud/container so saves go to iCloud, with room to spare, until a player adds an email.");
		}
	}
	// CloudKit needs the game's container in its entitlements (and in its provisioning profile).
	if (apple && !icloud_container.is_empty()) {
		const Ref<EditorExportPreset> preset = get_export_preset();
		const String option = p_features.has("macos") ? "codesign/entitlements/additional" : "entitlements/additional";
		const String additional = preset.is_valid() ? String(preset->get(option)) : String();
		if (!additional.contains("com.apple.developer.icloud-services") || !additional.contains(icloud_container)) {
			WARN_PRINT(vformat("iCloud saves are on (icloud/container is %s), but the export preset's \"%s\" doesn't give the game that iCloud container, so iCloud saves won't work. See the ICloud class reference for what to add.", icloud_container, option));
		}
	}
	// Purr keeps device secrets and remembered guests in the Keychain.
	if (p_features.has("ios") || p_features.has("tvos") || p_features.has("visionos")) {
		add_apple_embedded_platform_framework("Security.framework");
		add_apple_embedded_platform_framework("GameKit.framework");
		add_apple_embedded_platform_framework("CloudKit.framework");
		// The App Store module's StoreKit 2 code is in every Apple build.
		add_apple_embedded_platform_framework("StoreKit.framework");
		// Shown when Friends first asks Game Center for the player's friends.
		add_apple_embedded_platform_plist_content("<key>NSGKFriendListUsageDescription</key><string>Shows your Game Center friends in the game, so you can invite them.</string>");
	}
	// Plugins only reach the game through Godot's Gradle build.
	const Ref<EditorExportPreset> android_preset = get_export_preset();
	if (p_features.has("android") && play_games_app_id().is_empty() && android_preset.is_valid() && _google_play_export(android_preset->get_platform()) && !(bool)android_preset->get("gradle_build/use_gradle_build")) {
		WARN_PRINT("Store items are sold on Google Play, but this Android preset doesn't use the Gradle build, so Google Play's store won't be in the game. Turn on Gradle Build > Use Gradle Build in the preset.");
	}
	if (p_features.has("android") && !play_games_app_id().is_empty()) {
		if (!play_games_app_id().is_valid_int()) {
			ERR_PRINT(vformat("play_games/app_id must be the number the Play Console shows for the game, not \"%s\".", play_games_app_id()));
		}
		const Ref<EditorExportPreset> preset = get_export_preset();
		if (preset.is_valid() && !(bool)preset->get("gradle_build/use_gradle_build")) {
			WARN_PRINT("Play Games is on (play_games/app_id is set), but this Android preset doesn't use the Gradle build, so Play Games won't be in the game. Turn on Gradle Build > Use Gradle Build in the preset.");
		}
	}
#ifdef MODULE_GODOTSTEAM_ENABLED
	_export_steam(p_features);
#endif
}

bool PurrExportPlugin::_play_games_export(const Ref<EditorExportPlatform> &p_platform) const {
	return p_platform.is_valid() && p_platform->get_os_name() == "Android" && play_games_app_id().is_valid_int();
}

// Purr's Android library also holds Google Play's store: it goes in for Play Games, for items sold
// on Google Play, or for proving purchases to the online service.
bool PurrExportPlugin::_google_play_export(const Ref<EditorExportPlatform> &p_platform) const {
	if (p_platform.is_null() || p_platform->get_os_name() != "Android") {
		return false;
	}
	if (play_games_app_id().is_valid_int() || !String(GLOBAL_GET("google_play/cloud_project_number")).strip_edges().is_empty()) {
		return true;
	}
	List<PropertyInfo> properties;
	ProjectSettings::get_singleton()->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (property.name.begins_with("store/") && property.name.ends_with("/google_play") && !String(GLOBAL_GET(property.name)).strip_edges().is_empty()) {
			return true;
		}
	}
	return false;
}

PackedStringArray PurrExportPlugin::get_android_libraries(const Ref<EditorExportPlatform> &p_platform, bool p_debug) const {
	PackedStringArray libraries;
	if (_google_play_export(p_platform)) {
		// Built with Purr's Android export templates.
		const String library = templates_dir().path_join(PLAY_GAMES_LIBRARY);
		ERR_FAIL_COND_V_MSG(!FileAccess::exists(library), libraries, vformat("Google Play's plugin is missing from the export templates (%s). Reinstall Purr's export templates.", library));
		libraries.push_back(library);
	}
	return libraries;
}

PackedStringArray PurrExportPlugin::get_android_dependencies(const Ref<EditorExportPlatform> &p_platform, bool p_debug) const {
	PackedStringArray dependencies;
	if (_google_play_export(p_platform)) {
		// The versions the plugin is built against (modules/play_games/android/build.gradle): all
		// of them, since the library's classes use each.
		dependencies.push_back("com.google.android.gms:play-services-games-v2:22.1.0");
		dependencies.push_back("com.android.billingclient:billing:9.1.0");
		dependencies.push_back("com.google.android.play:integrity:1.6.0");
	}
	return dependencies;
}

String PurrExportPlugin::get_android_manifest_application_element_contents(const Ref<EditorExportPlatform> &p_platform, bool p_debug) const {
	if (!_play_games_export(p_platform)) {
		return String();
	}
	// Play Games reads the ID as a string, but Android would read a plain number as an integer:
	// "\u003" and the first digit make an escaped digit, which keeps it a string.
	return vformat("\n        <meta-data android:name=\"com.google.android.gms.games.APP_ID\" android:value=\"\\u003%s\" />\n", play_games_app_id());
}

void PurrExportPlugin::_export_steam(const HashSet<String> &p_features) {
	// The engine is linked against Steam's library, so every desktop export needs it next to the
	// game's binary, whether or not the game uses Steam. It only connects when Steam is running.
	// It ships with Purr's export templates.
	String library;
	String target;
	if (p_features.has("macos")) {
		library = "libsteam_api.dylib";
		target = "Contents/MacOS";
	} else if (p_features.has("windows")) {
		library = "steam_api64.dll";
	} else if (p_features.has("linux")) {
		library = "libsteam_api.so";
	} else {
		return;
	}

	const String library_path = templates_dir().path_join(library);
	ERR_FAIL_COND_MSG(!FileAccess::exists(library_path), vformat("Steam's library is missing from the export templates (%s), so the exported game won't start. Reinstall Purr's export templates.", library_path));
	add_shared_object(library_path, Vector<String>(), target);
}
