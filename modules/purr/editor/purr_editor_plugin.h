#pragma once

// Purr in the editor: the Achievements, Leaderboards and Store tabs in Project Settings.

#include "editor/plugins/editor_plugin.h"

class PlatformNamesSettings;

class PurrEditorPlugin : public EditorPlugin {
	GDCLASS(PurrEditorPlugin, EditorPlugin);

	PlatformNamesSettings *achievements = nullptr;
	PlatformNamesSettings *leaderboards = nullptr;
	PlatformNamesSettings *store = nullptr;

public:
	virtual String get_plugin_name() const override { return "Purr"; }

	PurrEditorPlugin();
	~PurrEditorPlugin();
};
