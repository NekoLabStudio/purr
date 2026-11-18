#include "behavior_call_method.h"

#include "core/object/class_db.h"
#include "scene/main/node.h"

BehaviorTask::Status BehaviorCallMethod::tick(double p_delta) {
	Node *agent = get_agent();
	Node *target = agent != nullptr && !node.is_empty() ? agent->get_node_or_null(node) : agent;
	ERR_FAIL_NULL_V_MSG(target, FAILURE, vformat("%s: there's no node to call %s() on.", get_display_title(), method));
	ERR_FAIL_COND_V_MSG(!target->has_method(method), FAILURE, vformat("%s: %s has no method %s().", get_display_title(), target->get_name(), method));
	const Variant result = target->callv(method, arguments);
	return result.get_type() == Variant::BOOL && !bool(result) ? FAILURE : SUCCESS;
}

String BehaviorCallMethod::describe() const {
	if (method == StringName()) {
		return String();
	}
	return node.is_empty() ? vformat("%s()", method) : vformat("%s.%s()", String(node), method);
}

void BehaviorCallMethod::set_node(const NodePath &p_node) {
	node = p_node;
	emit_changed();
}

NodePath BehaviorCallMethod::get_node() const {
	return node;
}

void BehaviorCallMethod::set_method(const StringName &p_method) {
	method = p_method;
	emit_changed();
}

StringName BehaviorCallMethod::get_method() const {
	return method;
}

void BehaviorCallMethod::set_arguments(const Array &p_arguments) {
	arguments = p_arguments;
	emit_changed();
}

Array BehaviorCallMethod::get_arguments() const {
	return arguments;
}

void BehaviorCallMethod::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_node", "node"), &BehaviorCallMethod::set_node);
	ClassDB::bind_method(D_METHOD("get_node"), &BehaviorCallMethod::get_node);
	ClassDB::bind_method(D_METHOD("set_method", "method"), &BehaviorCallMethod::set_method);
	ClassDB::bind_method(D_METHOD("get_method"), &BehaviorCallMethod::get_method);
	ClassDB::bind_method(D_METHOD("set_arguments", "arguments"), &BehaviorCallMethod::set_arguments);
	ClassDB::bind_method(D_METHOD("get_arguments"), &BehaviorCallMethod::get_arguments);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "node"), "set_node", "get_node");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "method"), "set_method", "get_method");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "arguments"), "set_arguments", "get_arguments");
}
