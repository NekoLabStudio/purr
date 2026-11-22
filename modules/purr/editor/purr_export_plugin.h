#pragma once

#include "editor/export/editor_export_plugin.h"

// Adds what Purr's modules need at run time to every export, so games never add it by hand.
class PurrExportPlugin : public EditorExportPlugin {
	GDSOFTCLASS(PurrExportPlugin, EditorExportPlugin);

	void _export_steam(const HashSet<String> &p_features);
	bool _play_games_export(const Ref<EditorExportPlatform> &p_platform) const;
	bool _google_play_export(const Ref<EditorExportPlatform> &p_platform) const;

protected:
	virtual void _export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) override;

public:
	virtual String get_name() const override { return "Purr"; }

	// Google Play Games: its plugin, the SDK and the game's application ID, in Android exports
	// when play_games/app_id is set.
	virtual PackedStringArray get_android_libraries(const Ref<EditorExportPlatform> &p_platform, bool p_debug) const override;
	virtual PackedStringArray get_android_dependencies(const Ref<EditorExportPlatform> &p_platform, bool p_debug) const override;
	virtual String get_android_manifest_application_element_contents(const Ref<EditorExportPlatform> &p_platform, bool p_debug) const override;
};
