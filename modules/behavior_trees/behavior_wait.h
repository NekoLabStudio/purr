#pragma once

// BehaviorWait: keeps running for seconds, then succeeds.

#include "behavior_task.h"

class BehaviorWait : public BehaviorTask {
	GDCLASS(BehaviorWait, BehaviorTask);

	double seconds = 1.0;

protected:
	static void _bind_methods();
	Status tick(double p_delta) override;
	String describe() const override;

public:
	void set_seconds(double p_seconds);
	double get_seconds() const;
};
