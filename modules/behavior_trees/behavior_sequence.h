#pragma once

// BehaviorSequence: runs its children in order while they succeed. It fails as soon as one fails, and succeeds once they all have.

#include "behavior_task.h"

class BehaviorSequence : public BehaviorTask {
	GDCLASS(BehaviorSequence, BehaviorTask);

	int current = 0;

protected:
	static void _bind_methods();
	Status tick(double p_delta) override;
	void on_enter() override { current = 0; }
};
