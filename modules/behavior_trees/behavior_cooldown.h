#pragma once

// BehaviorCooldown: runs its child, then fails without running it until seconds have passed since it last finished: to stop something happening too often.

#include "behavior_task.h"

class BehaviorCooldown : public BehaviorTask {
	GDCLASS(BehaviorCooldown, BehaviorTask);

	double seconds = 1.0;
	double ready_at = 0.0;

protected:
	static void _bind_methods();
	Status tick(double p_delta) override;
	String describe() const override;

public:
	void set_seconds(double p_seconds);
	double get_seconds() const;
};
