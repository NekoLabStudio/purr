#pragma once

// BehaviorCallMethod: calls a method on the agent (or on a node relative to it) with arguments. It fails if the method returns false, and succeeds otherwise.

#include "behavior_task.h"

class BehaviorCallMethod : public BehaviorTask {
	GDCLASS(BehaviorCallMethod, BehaviorTask);

	NodePath node;
	StringName method;
	Array arguments;

protected:
	static void _bind_methods();
	Status tick(double p_delta) override;
	String describe() const override;

public:
	void set_node(const NodePath &p_node);
	NodePath get_node() const;
	void set_method(const StringName &p_method);
	StringName get_method() const;
	void set_arguments(const Array &p_arguments);
	Array get_arguments() const;
};
