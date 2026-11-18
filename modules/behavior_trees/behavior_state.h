#pragma once

// BehaviorState: one state of a BehaviorStateMachine, as a node under it. While it's the active
// state, the machine calls its _update() (and _physics_update()) each tick; _enter() and _exit()
// are called as the machine switches to and from it. A state moves on with transition_to(), or
// by events the machine has transitions for.

#include "scene/main/node.h"

class BehaviorStateMachine;

class BehaviorState : public Node {
	GDCLASS(BehaviorState, Node);

	bool active = false;
	Dictionary blackboard; // Used on the outermost machine; the others share its.

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

	GDVIRTUAL0(_setup)
	GDVIRTUAL0(_enter)
	GDVIRTUAL0(_exit)
	GDVIRTUAL1(_update, double)

	virtual void on_enter() {}
	virtual void on_exit() {}
	virtual void on_update(double p_delta) {}

	const BehaviorStateMachine *_outermost() const;

public:
	BehaviorStateMachine *get_machine() const;
	Node *get_agent() const;
	Dictionary get_blackboard() const;
	void set_blackboard(const Dictionary &p_blackboard);
	bool is_active() const;

	void transition_to(const NodePath &p_state);
	virtual void send_event(const StringName &p_event);

	// For the machine.
	void setup();
	void enter();
	void exit();
	void update(double p_delta);
};
