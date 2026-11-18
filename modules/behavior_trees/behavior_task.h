#pragma once

// BehaviorTask: one step of a behaviour tree. Each tick, a running task returns RUNNING,
// SUCCESS or FAILURE; tasks with children (sequences, selectors and the like) decide which child
// runs. Games write their own actions and conditions by extending it and overriding _tick().
// A tree's tasks are copied for each BehaviorTreePlayer, so a task's variables belong to one
// agent.

#include "core/io/resource.h"
#include "core/object/gdvirtual.gen.h"
#include "core/variant/typed_array.h"
#include "scene/main/node.h"

// The time a running tree has been ticked for, shared by its tasks.
class BehaviorClock : public RefCounted {
	GDSOFTCLASS(BehaviorClock, RefCounted);

public:
	double time = 0.0;
};

class BehaviorTask : public Resource {
	GDCLASS(BehaviorTask, Resource);

public:
	enum Status {
		IDLE,
		RUNNING,
		SUCCESS,
		FAILURE,
	};

private:
	TypedArray<BehaviorTask> children;
	ObjectID agent_id;
	Dictionary blackboard;
	Ref<BehaviorClock> clock;
	Status status = IDLE;
	double elapsed = 0.0;

protected:
	static void _bind_methods();

	GDVIRTUAL0(_setup)
	GDVIRTUAL0(_enter)
	GDVIRTUAL0(_exit)
	// Returns a Status: an int here, since the enum's type isn't known until the class is.
	GDVIRTUAL1R(int, _tick, double)

	// What a task does each tick it runs. Built-in tasks override this; scripts override _tick().
	virtual Status tick(double p_delta);
	virtual void on_enter() {}
	virtual void on_exit() {}

	// What a built-in task says about itself in its title ("Wait 2 s"); empty for its class's name.
	virtual String describe() const { return String(); }
	// The default title: the class's name without "Behavior", in words.
	String get_default_title() const;

public:
	void set_children(const TypedArray<BehaviorTask> &p_children);
	TypedArray<BehaviorTask> get_children() const;
	int get_child_count() const;
	Ref<BehaviorTask> get_child(int p_index) const;
	// What the task is called in messages: what it does, or its class or script's name.
	String get_display_title() const;

	Node *get_agent() const;
	Dictionary get_blackboard() const;
	Status get_status() const;
	double get_elapsed_time() const;

	// A copy of this task and its children, sharing any other resources they hold.
	Ref<BehaviorTask> copy_tree() const;
	void initialize(Node *p_agent, const Dictionary &p_blackboard, const Ref<BehaviorClock> &p_clock);
	// The running tree's time, which only passes while it's ticked (and the game isn't paused).
	double get_tree_time() const { return clock.is_valid() ? clock->time : 0.0; }
	void advance_clock(double p_delta) {
		if (clock.is_valid()) {
			clock->time += p_delta;
		}
	}
	Status execute(double p_delta);
	void abort();
};

VARIANT_ENUM_CAST(BehaviorTask::Status);
