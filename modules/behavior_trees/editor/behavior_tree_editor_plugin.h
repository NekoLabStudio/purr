#pragma once

// Behaviour trees in the editor: the Behavior Tree dock opens for a BehaviorTree, for a
// BehaviorTreePlayer with a tree, and stays on its tree while one of its tasks is inspected.

#include "editor/plugins/editor_plugin.h"

class BehaviorTreeDebuggerPlugin;
class BehaviorTreeEditor;

class BehaviorTreeEditorPlugin : public EditorPlugin {
	GDCLASS(BehaviorTreeEditorPlugin, EditorPlugin);

	BehaviorTreeEditor *editor = nullptr;
	Ref<BehaviorTreeDebuggerPlugin> debugger;

public:
	virtual String get_plugin_name() const override { return "BehaviorTree"; }
	virtual void edit(Object *p_object) override;
	virtual bool handles(Object *p_object) const override;
	virtual void make_visible(bool p_visible) override;
	// Stays open while a task is inspected, rather than closing when the inspector moves on.
	virtual bool can_auto_hide() const override { return false; }

	BehaviorTreeEditorPlugin();
	~BehaviorTreeEditorPlugin();
};
