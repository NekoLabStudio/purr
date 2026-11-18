#pragma once

// The editor's side of the Behavior Tree dock's live view: tells running games which tree the
// dock shows, and passes their reports of its tasks' statuses to the dock.

#include "editor/debugger/editor_debugger_plugin.h"

class BehaviorTreeEditor;

class BehaviorTreeDebuggerPlugin : public EditorDebuggerPlugin {
	GDCLASS(BehaviorTreeDebuggerPlugin, EditorDebuggerPlugin);

	BehaviorTreeEditor *editor = nullptr;
	String watched_path;

	void _session_started(int p_session);
	void _session_stopped();

public:
	void set_editor(BehaviorTreeEditor *p_editor) { editor = p_editor; }
	void watch(const String &p_tree_path);

	virtual void setup_session(int p_session) override;
	virtual bool has_capture(const String &p_capture) const override;
	virtual bool capture(const String &p_message, const Array &p_data, int p_session) override;
};
