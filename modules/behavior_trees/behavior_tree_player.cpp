#include "behavior_tree_player.h"

#include "behavior_tree_debugger.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/os/os.h"

void BehaviorTreePlayer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY:
			if (!Engine::get_singleton()->is_editor_hint()) {
				_start();
				_update_processing();
			}
			break;
		case NOTIFICATION_PROCESS:
			update(get_process_delta_time());
			break;
		case NOTIFICATION_PHYSICS_PROCESS:
			update(get_physics_process_delta_time());
			break;
		case NOTIFICATION_EXIT_TREE:
			if (running.is_valid()) {
				running->abort();
			}
			break;
	}
}

// The tree's blackboard values the player doesn't set are added to the player's own, which the
// tasks then share: the game reads and writes the same dictionary.
void BehaviorTreePlayer::_start() {
	running.unref();
	last_status = BehaviorTask::IDLE;
	if (behavior_tree.is_null() || behavior_tree->get_root().is_null()) {
		return;
	}
	const Dictionary defaults = behavior_tree->get_blackboard();
	for (const KeyValue<Variant, Variant> &entry : defaults) {
		if (!blackboard.has(entry.key)) {
			blackboard[entry.key] = entry.value.duplicate(true);
		}
	}
	running = behavior_tree->instantiate(get_agent_node(), blackboard);
}

void BehaviorTreePlayer::_update_processing() {
	const bool ticking = active && is_inside_tree() && !Engine::get_singleton()->is_editor_hint();
	set_process(ticking && update_mode == UPDATE_PROCESS);
	set_physics_process(ticking && update_mode == UPDATE_PHYSICS);
}

BehaviorTask::Status BehaviorTreePlayer::update(double p_delta) {
	if (running.is_null()) {
		return BehaviorTask::IDLE;
	}
	// Held while it runs: a task may restart the tree, or give the player another tree, agent or
	// blackboard, which starts afresh and drops this one.
	const Ref<BehaviorTask> root = running;
	root->advance_clock(p_delta);
	const BehaviorTask::Status status = root->execute(p_delta);
	if (running != root) {
		return last_status;
	}
	last_status = status;
#ifdef DEBUG_ENABLED
	if (behavior_tree.is_valid() && BehaviorTreeDebugger::is_watching(behavior_tree->get_path())) {
		const uint64_t now = OS::get_singleton()->get_ticks_usec();
		if (now - last_report_usec > REPORT_EVERY_USEC) {
			last_report_usec = now;
			BehaviorTreeDebugger::report(this);
		}
	}
#endif
	if (last_status != BehaviorTask::RUNNING) {
		emit_signal(SNAME("finished"), last_status);
	}
	return last_status;
}

void BehaviorTreePlayer::restart() {
	if (running.is_valid()) {
		running->abort();
	}
	if (is_inside_tree()) {
		_start();
	}
}

BehaviorTask::Status BehaviorTreePlayer::get_last_status() const {
	return last_status;
}

Ref<BehaviorTask> BehaviorTreePlayer::get_running_tree() const {
	return running;
}

PackedStringArray BehaviorTreePlayer::get_configuration_warnings() const {
	PackedStringArray warnings = Node::get_configuration_warnings();
	if (behavior_tree.is_null()) {
		warnings.push_back(RTR("Set the behavior tree to run."));
	} else if (behavior_tree->get_root().is_null()) {
		warnings.push_back(RTR("The behavior tree has no root task."));
	}
	return warnings;
}

void BehaviorTreePlayer::set_behavior_tree(const Ref<BehaviorTree> &p_tree) {
	behavior_tree = p_tree;
	update_configuration_warnings();
	if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
		restart();
	}
}

Ref<BehaviorTree> BehaviorTreePlayer::get_behavior_tree() const {
	return behavior_tree;
}

void BehaviorTreePlayer::set_agent(const NodePath &p_agent) {
	agent = p_agent;
	if (is_inside_tree() && !Engine::get_singleton()->is_editor_hint()) {
		restart();
	}
}

NodePath BehaviorTreePlayer::get_agent() const {
	return agent;
}

Node *BehaviorTreePlayer::get_agent_node() const {
	return agent.is_empty() ? get_parent() : get_node_or_null(agent);
}

void BehaviorTreePlayer::set_blackboard(const Dictionary &p_blackboard) {
	blackboard = p_blackboard;
	if (running.is_valid()) {
		restart();
	}
}

Dictionary BehaviorTreePlayer::get_blackboard() const {
	return blackboard;
}

void BehaviorTreePlayer::set_update_mode(UpdateMode p_mode) {
	update_mode = p_mode;
	_update_processing();
}

BehaviorTreePlayer::UpdateMode BehaviorTreePlayer::get_update_mode() const {
	return update_mode;
}

void BehaviorTreePlayer::set_active(bool p_active) {
	if (active == p_active) {
		return;
	}
	active = p_active;
	if (!active && running.is_valid()) {
		running->abort();
	}
	_update_processing();
}

bool BehaviorTreePlayer::is_active() const {
	return active;
}

void BehaviorTreePlayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_behavior_tree", "tree"), &BehaviorTreePlayer::set_behavior_tree);
	ClassDB::bind_method(D_METHOD("get_behavior_tree"), &BehaviorTreePlayer::get_behavior_tree);
	ClassDB::bind_method(D_METHOD("set_agent", "agent"), &BehaviorTreePlayer::set_agent);
	ClassDB::bind_method(D_METHOD("get_agent"), &BehaviorTreePlayer::get_agent);
	ClassDB::bind_method(D_METHOD("get_agent_node"), &BehaviorTreePlayer::get_agent_node);
	ClassDB::bind_method(D_METHOD("set_blackboard", "blackboard"), &BehaviorTreePlayer::set_blackboard);
	ClassDB::bind_method(D_METHOD("get_blackboard"), &BehaviorTreePlayer::get_blackboard);
	ClassDB::bind_method(D_METHOD("set_update_mode", "mode"), &BehaviorTreePlayer::set_update_mode);
	ClassDB::bind_method(D_METHOD("get_update_mode"), &BehaviorTreePlayer::get_update_mode);
	ClassDB::bind_method(D_METHOD("set_active", "active"), &BehaviorTreePlayer::set_active);
	ClassDB::bind_method(D_METHOD("is_active"), &BehaviorTreePlayer::is_active);
	ClassDB::bind_method(D_METHOD("update", "delta"), &BehaviorTreePlayer::update);
	ClassDB::bind_method(D_METHOD("restart"), &BehaviorTreePlayer::restart);
	ClassDB::bind_method(D_METHOD("get_last_status"), &BehaviorTreePlayer::get_last_status);
	ClassDB::bind_method(D_METHOD("get_running_tree"), &BehaviorTreePlayer::get_running_tree);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "behavior_tree", PROPERTY_HINT_RESOURCE_TYPE, "BehaviorTree"), "set_behavior_tree", "get_behavior_tree");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "agent"), "set_agent", "get_agent");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "blackboard"), "set_blackboard", "get_blackboard");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "update_mode", PROPERTY_HINT_ENUM, "Process,Physics,Manual"), "set_update_mode", "get_update_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "active"), "set_active", "is_active");

	ADD_SIGNAL(MethodInfo("finished", PropertyInfo(Variant::INT, "status", PROPERTY_HINT_ENUM, "Idle,Running,Success,Failure", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM, "BehaviorTask.Status")));

	BIND_ENUM_CONSTANT(UPDATE_PROCESS);
	BIND_ENUM_CONSTANT(UPDATE_PHYSICS);
	BIND_ENUM_CONSTANT(UPDATE_MANUAL);
}
