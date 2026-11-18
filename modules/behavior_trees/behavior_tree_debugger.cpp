#include "behavior_tree_debugger.h"

#include "behavior_tree_player.h"

#include "core/debugger/engine_debugger.h"

String BehaviorTreeDebugger::watched_path;

void BehaviorTreeDebugger::initialize() {
	EngineDebugger::register_message_capture(CAPTURE, EngineDebugger::Capture(nullptr, &BehaviorTreeDebugger::_capture));
}

void BehaviorTreeDebugger::finalize() {
	if (EngineDebugger::has_capture(CAPTURE)) {
		EngineDebugger::unregister_message_capture(CAPTURE);
	}
}

// "watch" with a tree's path starts the reports for it; with an empty path, stops them.
Error BehaviorTreeDebugger::_capture(void *p_user, const String &p_message, const Array &p_data, bool &r_captured) {
	r_captured = true;
	if (p_message == "watch") {
		watched_path = p_data.is_empty() ? String() : String(p_data[0]);
		return OK;
	}
	r_captured = false;
	return ERR_SKIP;
}

bool BehaviorTreeDebugger::is_watching(const String &p_tree_path) {
	return !watched_path.is_empty() && watched_path == p_tree_path && EngineDebugger::is_active();
}

namespace {
void collect(const Ref<BehaviorTask> &p_task, PackedInt32Array &r_statuses) {
	r_statuses.push_back(p_task.is_valid() ? int(p_task->get_status()) : int(BehaviorTask::IDLE));
	for (int i = 0; p_task.is_valid() && i < p_task->get_child_count(); i++) {
		collect(p_task->get_child(i), r_statuses);
	}
}
} // namespace

// The tree's path, the player's path in the scene, its agent's name and every task's status, in
// the tree's order (each task, then its children).
void BehaviorTreeDebugger::report(BehaviorTreePlayer *p_player) {
	PackedInt32Array statuses;
	collect(p_player->get_running_tree(), statuses);
	Array data;
	data.push_back(watched_path);
	data.push_back(String(p_player->get_path()));
	const Node *agent = p_player->get_agent_node();
	data.push_back(String(agent != nullptr ? agent->get_name() : p_player->get_name()));
	data.push_back(statuses);
	EngineDebugger::get_singleton()->send_message(String(CAPTURE) + ":status", data);
}
