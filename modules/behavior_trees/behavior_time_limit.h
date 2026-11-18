#pragma once

// BehaviorTimeLimit: runs its child, and stops it and fails if it's still running after seconds.

#include "behavior_task.h"

class BehaviorTimeLimit : public BehaviorTask {
	GDCLASS(BehaviorTimeLimit, BehaviorTask);

	double seconds = 5.0;

protected:
	static void _bind_methods();
	Status tick(double p_delta) override;
	String describe() const override;

public:
	void set_seconds(double p_seconds);
	double get_seconds() const;
};
