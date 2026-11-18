#pragma once

// BehaviorRepeat: runs its child again each time it finishes, a number of times or for ever (times 0), once a tick at most. If the child fails, so does the repeat, unless ignore_failure is on.

#include "behavior_task.h"

class BehaviorRepeat : public BehaviorTask {
	GDCLASS(BehaviorRepeat, BehaviorTask);

	int times = 0;
	bool ignore_failure = false;
	int done = 0;

protected:
	static void _bind_methods();
	Status tick(double p_delta) override;
	void on_enter() override { done = 0; }
	String describe() const override;

public:
	void set_times(int p_times);
	int get_times() const;
	void set_ignore_failure(bool p_ignore);
	bool is_ignoring_failure() const;
};
