#pragma once

// BehaviorTree: a tree of tasks to save and share, with the blackboard values it starts with.
// A BehaviorTreePlayer runs a copy of it for one agent.

#include "behavior_task.h"

class BehaviorTree : public Resource {
	GDCLASS(BehaviorTree, Resource);

	Ref<BehaviorTask> root;
	Dictionary blackboard;

protected:
	static void _bind_methods();

public:
	void set_root(const Ref<BehaviorTask> &p_root);
	Ref<BehaviorTask> get_root() const;
	void set_blackboard(const Dictionary &p_blackboard);
	Dictionary get_blackboard() const;

	Ref<BehaviorTask> instantiate(Node *p_agent, const Dictionary &p_blackboard) const;
};
