#include "behavior_tree_debugger_plugin.h"

#include "../behavior_tree_debugger.h"
#include "behavior_tree_editor.h"

#include "core/object/callable_mp.h"

void BehaviorTreeDebuggerPlugin::setup_session(int p_session) {
	Ref<EditorDebuggerSession> session = get_session(p_session);
	if (session.is_valid()) {
		session->connect("started", callable_mp(this, &BehaviorTreeDebuggerPlugin::_session_started).bind(p_session));
		session->connect("stopped", callable_mp(this, &BehaviorTreeDebuggerPlugin::_session_stopped));
	}
}

// A game that starts while the dock shows a tree is told to report on it.
void BehaviorTreeDebuggerPlugin::_session_started(int p_session) {
	Ref<EditorDebuggerSession> session = get_session(p_session);
	if (session.is_valid() && !watched_path.is_empty()) {
		session->send_message(String(BehaviorTreeDebugger::CAPTURE) + ":watch", { watched_path });
	}
}

void BehaviorTreeDebuggerPlugin::_session_stopped() {
	if (editor != nullptr) {
		editor->clear_live();
	}
}

void BehaviorTreeDebuggerPlugin::watch(const String &p_tree_path) {
	watched_path = p_tree_path;
	const Array sessions = get_sessions();
	for (int i = 0; i < sessions.size(); i++) {
		Ref<EditorDebuggerSession> session = sessions[i];
		if (session.is_valid() && session->is_active()) {
			session->send_message(String(BehaviorTreeDebugger::CAPTURE) + ":watch", { watched_path });
		}
	}
}

bool BehaviorTreeDebuggerPlugin::has_capture(const String &p_capture) const {
	return p_capture == BehaviorTreeDebugger::CAPTURE;
}

bool BehaviorTreeDebuggerPlugin::capture(const String &p_message, const Array &p_data, int p_session) {
	if (p_message == String(BehaviorTreeDebugger::CAPTURE) + ":status" && p_data.size() == 4 && editor != nullptr) {
		editor->show_live(p_data[0], p_data[1], p_data[2], p_data[3]);
		return true;
	}
	return false;
}
