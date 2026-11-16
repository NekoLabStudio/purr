#include "vr_streamer_export_plugin.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/version.h"
#include "editor/export/editor_export_preset.h"
#include "editor/file_system/editor_paths.h"

// A preset option, so the streamer ships only where it's used: on by default with OpenXR on, and
// for a game that turns OpenXR on as it starts (through a settings override or --xr-mode), whose
// project leaves it off.
void VRStreamerExportPlugin::_get_export_options(const Ref<EditorExportPlatform> &p_export_platform, List<EditorExportPlatform::ExportOption> *r_options) const {
	if (p_export_platform->get_os_name() == "macOS") {
		r_options->push_back(EditorExportPlatform::ExportOption(PropertyInfo(Variant::BOOL, "xr/include_vr_runtime"), (bool)GLOBAL_GET("xr/openxr/enabled")));
	}
}

// What the runtime and streamer need of a Mac game with VR, set for it: recording the game's sound
// (the hardened runtime asks for the audio input entitlement) and, in the App Sandbox, the
// network both ways (the streamer connects to the headset and listens for it).
Dictionary VRStreamerExportPlugin::_get_export_options_overrides(const Ref<EditorExportPlatform> &p_export_platform) const {
	Dictionary overrides;
	if (p_export_platform->get_os_name() == "macOS" && (bool)GLOBAL_GET("xr/openxr/enabled")) {
		overrides["codesign/entitlements/audio_input"] = true;
		overrides["codesign/entitlements/app_sandbox/network_client"] = true;
		overrides["codesign/entitlements/app_sandbox/network_server"] = true;
	}
	return overrides;
}

void VRStreamerExportPlugin::_export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) {
	if (!p_features.has("macos") || !(bool)get_option("xr/include_vr_runtime")) {
		return;
	}
	// Everything the streamer needs ships in a folder of Purr's export templates.
	const String folder = EditorPaths::get_singleton()->get_export_templates_dir().path_join(GODOT_VERSION_FULL_CONFIG).path_join("vr_streamer").path_join("macos");
	Ref<DirAccess> dir = DirAccess::open(folder);
	ERR_FAIL_COND_MSG(dir.is_null(), vformat("Purr's VR streamer is missing from the export templates (%s), so the game won't stream to a headset. Reinstall Purr's export templates.", folder));
	// The streamer sends the game's own sound to the headset through a Core Audio tap, which
	// macOS only allows (and only asks the player about) when the app describes why.
	// A key the export preset already sets is left to it.
	const Ref<EditorExportPreset> preset = get_export_preset();
	const String own_plist = preset.is_valid() ? String(preset->get("application/additional_plist_content")) : String();
	const bool own_microphone = preset.is_valid() && !String(preset->get("privacy/microphone_usage_description")).is_empty();
	const String game = String(GLOBAL_GET("application/config/name")).xml_escape(true);
	if (!own_plist.contains("NSAudioCaptureUsageDescription")) {
		add_macos_plist_content(vformat("<key>NSAudioCaptureUsageDescription</key><string>%s sends its own sound to your headset while you play in VR.</string>", game));
	}
	// It also finds headsets on the local network (Steam Link's, and a Vision Pro).
	if (!own_plist.contains("NSLocalNetworkUsageDescription")) {
		add_macos_plist_content(vformat("<key>NSLocalNetworkUsageDescription</key><string>%s looks for VR headsets on your network.</string>", game));
	}
	// The Bonjour service the streamer looks for a Vision Pro with, which macOS asks apps to declare.
	if (!own_plist.contains("NSBonjourServices")) {
		add_macos_plist_content("<key>NSBonjourServices</key><array><string>_companion-link._tcp</string></array>");
	}
	if (!own_microphone && !own_plist.contains("NSMicrophoneUsageDescription")) {
		add_macos_plist_content(vformat("<key>NSMicrophoneUsageDescription</key><string>%s records its own sound for your VR headset. It never records your microphone.</string>", game));
	}
	for (const String &file : dir->get_files()) {
		// A Mac app's code folder holds only code: licence texts go with its resources.
		const bool text = file.get_extension() == "txt";
		add_shared_object(folder.path_join(file), Vector<String>(), text ? String("Contents/Resources") : String("Contents/MacOS"));
	}
}
