#pragma once

// BehaviorTreePlayer: runs a behaviour tree for an agent (its parent, unless set), a tick a frame
// or a physics tick. When the tree finishes it starts again on the next tick, so it keeps
// deciding what the agent does. Its blackboard holds the values the tasks share, starting with
// the tree's own, overridden by the player's.

#include "behavior_tree.h"

#include "scene/main/node.h"

class BehaviorTreePlayer : public Node {
	GDCLASS(BehaviorTreePlayer, Node);

public:
	enum UpdateMode {
		UPDATE_PROCESS,
		UPDATE_PHYSICS,
		UPDATE_MANUAL,
	};

private:
	Ref<BehaviorTree> behavior_tree;
	NodePath agent = NodePath("..");
	Dictionary blackboard;
	UpdateMode update_mode = UPDATE_PHYSICS;
	bool active = true;

	Ref<BehaviorTask> running; // This agent's copy of the tree.
	BehaviorTask::Status last_status = BehaviorTask::IDLE;
	uint64_t last_report_usec = 0; // To the editor's Behavior Tree dock, while it watches this tree.
	static constexpr uint64_t REPORT_EVERY_USEC = 100000;

	void _start();
	void _update_processing();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_behavior_tree(const Ref<BehaviorTree> &p_tree);
	Ref<BehaviorTree> get_behavior_tree() const;
	void set_agent(const NodePath &p_agent);
	NodePath get_agent() const;
	Node *get_agent_node() const;
	void set_blackboard(const Dictionary &p_blackboard);
	Dictionary get_blackboard() const;
	void set_update_mode(UpdateMode p_mode);
	UpdateMode get_update_mode() const;
	void set_active(bool p_active);
	bool is_active() const;

	BehaviorTask::Status update(double p_delta);
	void restart();
	BehaviorTask::Status get_last_status() const;
	Ref<BehaviorTask> get_running_tree() const;

	PackedStringArray get_configuration_warnings() const override;
};

VARIANT_ENUM_CAST(BehaviorTreePlayer::UpdateMode);
