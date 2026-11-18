#pragma once

// BehaviorParallel: runs all its children every tick. With FINISH_WHEN_ANY it finishes when the first child does, with that child's result, stopping the others; with FINISH_WHEN_ALL it waits for them all, and succeeds if they all succeeded.

#include "behavior_task.h"

class BehaviorParallel : public BehaviorTask {
	GDCLASS(BehaviorParallel, BehaviorTask);

public:
	enum FinishWhen {
		FINISH_WHEN_ANY,
		FINISH_WHEN_ALL,
	};

private:
	FinishWhen finish_when = FINISH_WHEN_ANY;
	Vector<Status> results;

protected:
	static void _bind_methods();
	Status tick(double p_delta) override;
	void on_enter() override;

public:
	void set_finish_when(FinishWhen p_when);
	FinishWhen get_finish_when() const;
};

VARIANT_ENUM_CAST(BehaviorParallel::FinishWhen);
