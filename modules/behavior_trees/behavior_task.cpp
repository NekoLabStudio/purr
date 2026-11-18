#include "behavior_task.h"

#include "core/object/class_db.h"
#include "core/object/script_language.h"

BehaviorTask::Status BehaviorTask::tick(double p_delta) {
	int returned = SUCCESS;
	if (GDVIRTUAL_CALL(_tick, p_delta, returned)) {
		Status result = Status(returned);
		if (result != RUNNING && result != SUCCESS && result != FAILURE) {
			ERR_PRINT_ONCE(vformat("A behaviour task's _tick() returned something other than RUNNING, SUCCESS or FAILURE: treating it as FAILURE (%s).", get_display_title()));
			result = FAILURE;
		}
		return result;
	}
	return SUCCESS;
}

BehaviorTask::Status BehaviorTask::execute(double p_delta) {
	if (status != RUNNING) {
		elapsed = 0.0;
		on_enter();
		GDVIRTUAL_CALL(_enter);
	} else {
		elapsed += p_delta;
	}
	status = tick(p_delta);
	if (status != RUNNING) {
		on_exit();
		GDVIRTUAL_CALL(_exit);
	}
	return status;
}

// Stops a running task (and its running children) without it finishing, as when a parent moves
// on: it's told it's leaving and goes back to idle.
void BehaviorTask::abort() {
	if (status != RUNNING) {
		status = IDLE;
		return;
	}
	for (int i = 0; i < children.size(); i++) {
		const Ref<BehaviorTask> child = children[i];
		if (child.is_valid()) {
			child->abort();
		}
	}
	on_exit();
	GDVIRTUAL_CALL(_exit);
	status = IDLE;
}

Ref<BehaviorTask> BehaviorTask::copy_tree() const {
	// Arrays and dictionaries copied too, so a task's lists are its own; resources shared.
	Ref<BehaviorTask> copy = duplicate_deep(RESOURCE_DEEP_DUPLICATE_NONE);
	TypedArray<BehaviorTask> copied_children;
	for (int i = 0; i < children.size(); i++) {
		const Ref<BehaviorTask> child = children[i];
		copied_children.push_back(child.is_valid() ? child->copy_tree() : Ref<BehaviorTask>());
	}
	copy->children = copied_children;
	return copy;
}

void BehaviorTask::initialize(Node *p_agent, const Dictionary &p_blackboard, const Ref<BehaviorClock> &p_clock) {
	clock = p_clock;
	agent_id = p_agent != nullptr ? p_agent->get_instance_id() : ObjectID();
	blackboard = p_blackboard;
	status = IDLE;
	elapsed = 0.0;
	for (int i = 0; i < children.size(); i++) {
		const Ref<BehaviorTask> child = children[i];
		if (child.is_valid()) {
			child->initialize(p_agent, p_blackboard, p_clock);
		}
	}
	GDVIRTUAL_CALL(_setup);
}

String BehaviorTask::get_default_title() const {
	String name = get_class();
	const Ref<Script> script = get_script();
	if (script.is_valid() && !script->get_global_name().is_empty()) {
		name = script->get_global_name();
	} else if (script.is_valid() && !script->get_path().is_empty()) {
		name = script->get_path().get_file().get_basename().to_pascal_case();
	}
	return name.trim_prefix("Behavior").capitalize();
}

String BehaviorTask::get_display_title() const {
	const String described = describe();
	return described.is_empty() ? get_default_title() : described;
}

void BehaviorTask::set_children(const TypedArray<BehaviorTask> &p_children) {
	children = p_children;
	emit_changed();
}

TypedArray<BehaviorTask> BehaviorTask::get_children() const {
	return children;
}

int BehaviorTask::get_child_count() const {
	return children.size();
}

Ref<BehaviorTask> BehaviorTask::get_child(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, children.size(), Ref<BehaviorTask>());
	return children[p_index];
}

Node *BehaviorTask::get_agent() const {
	return Object::cast_to<Node>(ObjectDB::get_instance(agent_id));
}

Dictionary BehaviorTask::get_blackboard() const {
	return blackboard;
}

BehaviorTask::Status BehaviorTask::get_status() const {
	return status;
}

double BehaviorTask::get_elapsed_time() const {
	return elapsed;
}

void BehaviorTask::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_children", "children"), &BehaviorTask::set_children);
	ClassDB::bind_method(D_METHOD("get_children"), &BehaviorTask::get_children);
	ClassDB::bind_method(D_METHOD("get_child_count"), &BehaviorTask::get_child_count);
	ClassDB::bind_method(D_METHOD("get_child", "index"), &BehaviorTask::get_child);
	ClassDB::bind_method(D_METHOD("get_agent"), &BehaviorTask::get_agent);
	ClassDB::bind_method(D_METHOD("get_blackboard"), &BehaviorTask::get_blackboard);
	ClassDB::bind_method(D_METHOD("get_status"), &BehaviorTask::get_status);
	ClassDB::bind_method(D_METHOD("get_elapsed_time"), &BehaviorTask::get_elapsed_time);

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "children", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("BehaviorTask")), "set_children", "get_children");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "agent", PROPERTY_HINT_NODE_TYPE, "Node", PROPERTY_USAGE_NONE), "", "get_agent");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "blackboard", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_blackboard");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "status", PROPERTY_HINT_ENUM, "Idle,Running,Success,Failure", PROPERTY_USAGE_NONE), "", "get_status");

	GDVIRTUAL_BIND(_setup);
	GDVIRTUAL_BIND(_enter);
	GDVIRTUAL_BIND(_exit);
	GDVIRTUAL_BIND(_tick, "delta");

	BIND_ENUM_CONSTANT(IDLE);
	BIND_ENUM_CONSTANT(RUNNING);
	BIND_ENUM_CONSTANT(SUCCESS);
	BIND_ENUM_CONSTANT(FAILURE);
}
