#include "behavior_tree.h"

#include "core/object/class_db.h"

void BehaviorTree::set_root(const Ref<BehaviorTask> &p_root) {
	root = p_root;
	emit_changed();
}

Ref<BehaviorTask> BehaviorTree::get_root() const {
	return root;
}

void BehaviorTree::set_blackboard(const Dictionary &p_blackboard) {
	blackboard = p_blackboard;
	emit_changed();
}

Dictionary BehaviorTree::get_blackboard() const {
	return blackboard;
}

// A copy of every task (including tasks saved in files of their own), so what a task keeps
// belongs to this agent alone. Other resources the tasks hold (sounds, scenes) are shared.
Ref<BehaviorTask> BehaviorTree::instantiate(Node *p_agent, const Dictionary &p_blackboard) const {
	ERR_FAIL_COND_V_MSG(root.is_null(), Ref<BehaviorTask>(), "This behaviour tree has no root task.");
	Ref<BehaviorTask> copy = root->copy_tree();
	Ref<BehaviorClock> clock;
	clock.instantiate();
	copy->initialize(p_agent, p_blackboard, clock);
	return copy;
}

void BehaviorTree::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_root", "root"), &BehaviorTree::set_root);
	ClassDB::bind_method(D_METHOD("get_root"), &BehaviorTree::get_root);
	ClassDB::bind_method(D_METHOD("set_blackboard", "blackboard"), &BehaviorTree::set_blackboard);
	ClassDB::bind_method(D_METHOD("get_blackboard"), &BehaviorTree::get_blackboard);
	ClassDB::bind_method(D_METHOD("instantiate", "agent", "blackboard"), &BehaviorTree::instantiate);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "root", PROPERTY_HINT_RESOURCE_TYPE, "BehaviorTask"), "set_root", "get_root");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "blackboard"), "set_blackboard", "get_blackboard");
}
