#include "behavior_state.h"

#include "behavior_state_machine.h"

#include "core/object/class_db.h"

BehaviorStateMachine *BehaviorState::get_machine() const {
	return Object::cast_to<BehaviorStateMachine>(get_parent());
}

// The machine at the top, whose agent and blackboard every state inside it shares.
const BehaviorStateMachine *BehaviorState::_outermost() const {
	const BehaviorStateMachine *outermost = Object::cast_to<BehaviorStateMachine>(const_cast<BehaviorState *>(this));
	for (Node *node = get_parent(); node != nullptr; node = node->get_parent()) {
		const BehaviorStateMachine *machine = Object::cast_to<BehaviorStateMachine>(node);
		if (machine == nullptr) {
			break;
		}
		outermost = machine;
	}
	return outermost;
}

// The node the states control: the outermost machine's parent.
Node *BehaviorState::get_agent() const {
	const BehaviorStateMachine *outermost = _outermost();
	return outermost != nullptr ? outermost->get_parent() : nullptr;
}

Dictionary BehaviorState::get_blackboard() const {
	const BehaviorStateMachine *outermost = _outermost();
	return outermost != nullptr ? outermost->blackboard : blackboard;
}

void BehaviorState::set_blackboard(const Dictionary &p_blackboard) {
	blackboard = p_blackboard;
}

// Set (and saved) on the outermost machine only.
void BehaviorState::_validate_property(PropertyInfo &p_property) const {
	if (p_property.name == "blackboard" && (Object::cast_to<BehaviorStateMachine>(const_cast<BehaviorState *>(this)) == nullptr || Object::cast_to<BehaviorStateMachine>(get_parent()) != nullptr)) {
		p_property.usage = PROPERTY_USAGE_NONE;
	}
}

bool BehaviorState::is_active() const {
	return active;
}

// A path from the machine: a sibling state's name, such as "Chase".
void BehaviorState::transition_to(const NodePath &p_state) {
	BehaviorStateMachine *machine = get_machine();
	ERR_FAIL_NULL_MSG(machine, vformat("%s isn't in a BehaviorStateMachine.", get_name()));
	machine->change_state(p_state);
}

// Its own machine has the first chance to take a transition for the event, then the machines
// round that one.
void BehaviorState::send_event(const StringName &p_event) {
	BehaviorStateMachine *machine = get_machine();
	ERR_FAIL_NULL_MSG(machine, vformat("%s isn't in a BehaviorStateMachine.", get_name()));
	machine->handle_event_upwards(p_event);
}

void BehaviorState::setup() {
	GDVIRTUAL_CALL(_setup);
	for (int i = 0; i < get_child_count(); i++) {
		if (BehaviorState *child = Object::cast_to<BehaviorState>(get_child(i))) {
			child->setup();
		}
	}
}

void BehaviorState::enter() {
	active = true;
	GDVIRTUAL_CALL(_enter);
	on_enter();
	emit_signal(SNAME("entered"));
}

void BehaviorState::exit() {
	if (!active) {
		return;
	}
	on_exit();
	GDVIRTUAL_CALL(_exit);
	active = false;
	emit_signal(SNAME("exited"));
}

void BehaviorState::update(double p_delta) {
	GDVIRTUAL_CALL(_update, p_delta);
	if (active) {
		on_update(p_delta);
	}
}

void BehaviorState::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_machine"), &BehaviorState::get_machine);
	ClassDB::bind_method(D_METHOD("get_agent"), &BehaviorState::get_agent);
	ClassDB::bind_method(D_METHOD("get_blackboard"), &BehaviorState::get_blackboard);
	ClassDB::bind_method(D_METHOD("is_active"), &BehaviorState::is_active);
	ClassDB::bind_method(D_METHOD("transition_to", "state"), &BehaviorState::transition_to);
	ClassDB::bind_method(D_METHOD("send_event", "event"), &BehaviorState::send_event);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "agent", PROPERTY_HINT_NODE_TYPE, "Node", PROPERTY_USAGE_NONE), "", "get_agent");
	ClassDB::bind_method(D_METHOD("set_blackboard", "blackboard"), &BehaviorState::set_blackboard);
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "blackboard"), "set_blackboard", "get_blackboard");

	ADD_SIGNAL(MethodInfo("entered"));
	ADD_SIGNAL(MethodInfo("exited"));

	GDVIRTUAL_BIND(_setup);
	GDVIRTUAL_BIND(_enter);
	GDVIRTUAL_BIND(_exit);
	GDVIRTUAL_BIND(_update, "delta");
}
