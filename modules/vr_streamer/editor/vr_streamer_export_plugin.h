#pragma once

#include "editor/export/editor_export_plugin.h"

// Adds Purr's VR runtime and the streamer beside the game's binary in macOS exports whose preset
// includes it (xr/include_vr_runtime: on by default in projects with OpenXR on, and for games
// that turn OpenXR on as they start).
class VRStreamerExportPlugin : public EditorExportPlugin {
	GDSOFTCLASS(VRStreamerExportPlugin, EditorExportPlugin);

protected:
	virtual void _export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) override;
	virtual void _get_export_options(const Ref<EditorExportPlatform> &p_export_platform, List<EditorExportPlatform::ExportOption> *r_options) const override;
	virtual Dictionary _get_export_options_overrides(const Ref<EditorExportPlatform> &p_export_platform) const override;
	// Godot asks only plugins that say they support a platform for their overrides.
	virtual bool supports_platform(const Ref<EditorExportPlatform> &p_export_platform) const override { return p_export_platform->get_os_name() == "macOS"; }

public:
	virtual String get_name() const override { return "VRStreamer"; }
};
