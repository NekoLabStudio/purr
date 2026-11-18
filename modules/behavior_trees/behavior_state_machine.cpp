#include "behavior_state_machine.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

bool BehaviorStateMachine::_is_outermost() const {
	return Object::cast_to<BehaviorStateMachine>(get_parent()) == nullptr;
}

void BehaviorStateMachine::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE:
			// Back in the tree after leaving it (ready only comes once): start again.
			if (started && _is_outermost() && !Engine::get_singleton()->is_editor_hint()) {
				enter();
				_update_processing();
			}
			break;
		case NOTIFICATION_READY:
			if (_is_outermost() && !Engine::get_singleton()->is_editor_hint()) {
				setup();
				started = true;
				enter();
				_update_processing();
			}
			break;
		case NOTIFICATION_PROCESS:
			tick(get_process_delta_time());
			break;
		case NOTIFICATION_PHYSICS_PROCESS:
			tick(get_physics_process_delta_time());
			break;
		case NOTIFICATION_EXIT_TREE:
			if (_is_outermost()) {
				exit();
			}
			break;
	}
}

// Only the outermost machine ticks itself; the ones inside it tick while they're active.
void BehaviorStateMachine::_update_processing() {
	const bool ticking = started && running && _is_outermost() && is_inside_tree();
	set_process(ticking && update_mode == UPDATE_PROCESS);
	set_physics_process(ticking && update_mode == UPDATE_PHYSICS);
}

void BehaviorStateMachine::tick(double p_delta) {
	if (is_active()) {
		update(p_delta);
	}
}

BehaviorState *BehaviorStateMachine::_state_at(const NodePath &p_path) const {
	BehaviorState *state = Object::cast_to<BehaviorState>(get_node_or_null(p_path));
	return state != nullptr && state->get_parent() == this ? state : nullptr;
}

BehaviorState *BehaviorStateMachine::_initial() const {
	if (!initial_state.is_empty()) {
		BehaviorState *state = _state_at(initial_state);
		ERR_FAIL_NULL_V_MSG(state, nullptr, vformat("%s's initial state %s isn't one of its states.", get_name(), String(initial_state)));
		return state;
	}
	for (int i = 0; i < get_child_count(); i++) {
		if (BehaviorState *state = Object::cast_to<BehaviorState>(get_child(i))) {
			return state;
		}
	}
	return nullptr;
}

void BehaviorStateMachine::on_enter() {
	BehaviorState *initial = _initial();
	if (initial != nullptr) {
		changing = true;
		current = initial->get_instance_id();
		initial->enter();
		emit_signal(SNAME("state_changed"), Variant(), initial);
		_change_finished();
	}
}

void BehaviorStateMachine::on_exit() {
	BehaviorState *state = get_current_state();
	if (state != nullptr) {
		state->exit();
	}
	current = ObjectID();
}

void BehaviorStateMachine::on_update(double p_delta) {
	BehaviorState *state = get_current_state();
	if (state != nullptr) {
		state->update(p_delta);
	}
}

BehaviorState *BehaviorStateMachine::get_current_state() const {
	return Object::cast_to<BehaviorState>(ObjectDB::get_instance(current));
}

// A state's name (or a path to it from the machine). Changing to the active state starts it again.
void BehaviorStateMachine::change_state(const NodePath &p_state) {
	BehaviorState *target = _state_at(p_state);
	ERR_FAIL_NULL_MSG(target, vformat("%s has no state %s.", get_name(), String(p_state)));
	ERR_FAIL_COND_MSG(!is_active(), vformat("%s isn't running, so it can't change state.", get_name()));
	// Asked for while changing (from a state's _enter() or _exit(), or a state_changed handler): it
	// follows once this change is over, so states enter and exit, and the signals come, in order.
	if (changing) {
		pending = target->get_instance_id();
		return;
	}
	changing = true;
	BehaviorState *from = get_current_state();
	if (from != nullptr) {
		from->exit();
	}
	current = target->get_instance_id();
	target->enter();
	emit_signal(SNAME("state_changed"), from, target);
	_change_finished();
}

void BehaviorStateMachine::_change_finished() {
	changing = false;
	BehaviorState *next = Object::cast_to<BehaviorState>(ObjectDB::get_instance(pending));
	pending = ObjectID();
	if (next != nullptr && is_active()) {
		change_state(get_path_to(next));
	}
}

void BehaviorStateMachine::add_transition(const NodePath &p_from, const NodePath &p_to, const StringName &p_event, const Callable &p_guard) {
	BehaviorState *to = _state_at(p_to);
	ERR_FAIL_NULL_MSG(to, vformat("%s has no state %s.", get_name(), String(p_to)));
	Transition transition;
	if (!p_from.is_empty()) {
		BehaviorState *from = _state_at(p_from);
		ERR_FAIL_NULL_MSG(from, vformat("%s has no state %s.", get_name(), String(p_from)));
		transition.from = from->get_instance_id();
	}
	transition.to = to->get_instance_id();
	transition.event = p_event;
	transition.guard = p_guard;
	transitions.push_back(transition);
}

// The first transition for the event from the active state (or from any state) whose guard,
// if it has one, says yes.
bool BehaviorStateMachine::_take_transition(const StringName &p_event) {
	if (!is_active()) {
		return false;
	}
	for (const Transition &transition : transitions) {
		if (transition.event != p_event || (transition.from.is_valid() && transition.from != current)) {
			continue;
		}
		if (transition.guard.is_valid() && !bool(transition.guard.call())) {
			continue;
		}
		BehaviorState *to = Object::cast_to<BehaviorState>(ObjectDB::get_instance(transition.to));
		if (to != nullptr) {
			change_state(get_path_to(to));
			return true;
		}
	}
	return false;
}

bool BehaviorStateMachine::handle_event_upwards(const StringName &p_event) {
	for (BehaviorStateMachine *machine = this; machine != nullptr; machine = machine->get_machine()) {
		if (machine->_take_transition(p_event)) {
			return true;
		}
	}
	return false;
}

// From the game: the deepest active machine inside this one has the first chance, then the ones
// round it, up to this one.
void BehaviorStateMachine::send_event(const StringName &p_event) {
	BehaviorStateMachine *deepest = this;
	while (BehaviorStateMachine *inner = Object::cast_to<BehaviorStateMachine>(deepest->get_current_state())) {
		deepest = inner;
	}
	for (BehaviorStateMachine *machine = deepest; machine != nullptr; machine = machine->get_machine()) {
		if (machine->_take_transition(p_event) || machine == this) {
			return;
		}
	}
}

PackedStringArray BehaviorStateMachine::get_configuration_warnings() const {
	PackedStringArray warnings = BehaviorState::get_configuration_warnings();
	bool any = false;
	for (int i = 0; i < get_child_count() && !any; i++) {
		any = Object::cast_to<BehaviorState>(get_child(i)) != nullptr;
	}
	if (!any) {
		warnings.push_back(RTR("Add BehaviorState nodes (or scripts extending it) as children: they're the machine's states."));
	}
	return warnings;
}

void BehaviorStateMachine::set_initial_state(const NodePath &p_state) {
	initial_state = p_state;
}

NodePath BehaviorStateMachine::get_initial_state() const {
	return initial_state;
}

void BehaviorStateMachine::set_update_mode(UpdateMode p_mode) {
	update_mode = p_mode;
	_update_processing();
}

BehaviorStateMachine::UpdateMode BehaviorStateMachine::get_update_mode() const {
	return update_mode;
}

void BehaviorStateMachine::set_running(bool p_running) {
	running = p_running;
	_update_processing();
}

bool BehaviorStateMachine::is_running() const {
	return running;
}

void BehaviorStateMachine::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_initial_state", "state"), &BehaviorStateMachine::set_initial_state);
	ClassDB::bind_method(D_METHOD("get_initial_state"), &BehaviorStateMachine::get_initial_state);
	ClassDB::bind_method(D_METHOD("set_update_mode", "mode"), &BehaviorStateMachine::set_update_mode);
	ClassDB::bind_method(D_METHOD("get_update_mode"), &BehaviorStateMachine::get_update_mode);
	ClassDB::bind_method(D_METHOD("set_running", "running"), &BehaviorStateMachine::set_running);
	ClassDB::bind_method(D_METHOD("is_running"), &BehaviorStateMachine::is_running);
	ClassDB::bind_method(D_METHOD("get_current_state"), &BehaviorStateMachine::get_current_state);
	ClassDB::bind_method(D_METHOD("change_state", "state"), &BehaviorStateMachine::change_state);
	ClassDB::bind_method(D_METHOD("add_transition", "from", "to", "event", "guard"), &BehaviorStateMachine::add_transition, DEFVAL(Callable()));
	ClassDB::bind_method(D_METHOD("tick", "delta"), &BehaviorStateMachine::tick);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "initial_state", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "BehaviorState"), "set_initial_state", "get_initial_state");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "update_mode", PROPERTY_HINT_ENUM, "Process,Physics,Manual"), "set_update_mode", "get_update_mode");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "running"), "set_running", "is_running");

	ADD_SIGNAL(MethodInfo("state_changed", PropertyInfo(Variant::OBJECT, "from", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, "BehaviorState"), PropertyInfo(Variant::OBJECT, "to", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, "BehaviorState")));

	BIND_ENUM_CONSTANT(UPDATE_PROCESS);
	BIND_ENUM_CONSTANT(UPDATE_PHYSICS);
	BIND_ENUM_CONSTANT(UPDATE_MANUAL);
}
