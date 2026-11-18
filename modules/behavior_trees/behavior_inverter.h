#pragma once

// BehaviorInverter: runs its child and swaps success and failure, like "not".

#include "behavior_task.h"

class BehaviorInverter : public BehaviorTask {
	GDCLASS(BehaviorInverter, BehaviorTask);

protected:
	static void _bind_methods();
	Status tick(double p_delta) override;
};
