#pragma once

// BehaviorSelector: tries its children in order until one succeeds, and succeeds with it. It fails if they all fail: fallbacks, best first.

#include "behavior_task.h"

class BehaviorSelector : public BehaviorTask {
	GDCLASS(BehaviorSelector, BehaviorTask);

	int current = 0;

protected:
	static void _bind_methods();
	Status tick(double p_delta) override;
	void on_enter() override { current = 0; }
};
