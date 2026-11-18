#pragma once

// BehaviorStateMachine: one state active at a time, out of the BehaviorState nodes under it. It
// starts in initial_state (or its first state), ticks the active one, and switches when a state
// calls transition_to() or an event has a transition. A machine is a state itself, so one can sit
// inside another: its own states run while it's the active one. The outermost machine's parent is
// the agent the states control, and its blackboard is the one they share.

#include "behavior_state.h"

class BehaviorStateMachine : public BehaviorState {
	GDCLASS(BehaviorStateMachine, BehaviorState);

public:
	enum UpdateMode {
		UPDATE_PROCESS,
		UPDATE_PHYSICS,
		UPDATE_MANUAL,
	};

private:
	struct Transition {
		ObjectID from; // None: from any state.
		ObjectID to;
		StringName event;
		Callable guard;
	};

	NodePath initial_state;
	UpdateMode update_mode = UPDATE_PHYSICS;
	bool running = true;

	ObjectID current;
	bool changing = false;
	ObjectID pending; // A change asked for during another, made once it's over.
	Vector<Transition> transitions;
	bool started = false;

	bool _is_outermost() const;
	BehaviorState *_state_at(const NodePath &p_path) const;
	BehaviorState *_initial() const;
	void _change_finished();
	void _update_processing();
	bool _take_transition(const StringName &p_event);

protected:
	void _notification(int p_what);
	static void _bind_methods();

	void on_enter() override;
	void on_exit() override;
	void on_update(double p_delta) override;

public:
	void set_initial_state(const NodePath &p_state);
	NodePath get_initial_state() const;
	void set_update_mode(UpdateMode p_mode);
	UpdateMode get_update_mode() const;
	void set_running(bool p_running);
	bool is_running() const;

	BehaviorState *get_current_state() const;
	void change_state(const NodePath &p_state);
	void add_transition(const NodePath &p_from, const NodePath &p_to, const StringName &p_event, const Callable &p_guard = Callable());
	virtual void send_event(const StringName &p_event) override;
	bool handle_event_upwards(const StringName &p_event);
	void tick(double p_delta);

	PackedStringArray get_configuration_warnings() const override;
};

VARIANT_ENUM_CAST(BehaviorStateMachine::UpdateMode);
