#include "behavior_inverter.h"

#include "core/object/class_db.h"

BehaviorTask::Status BehaviorInverter::tick(double p_delta) {
	const Ref<BehaviorTask> child = get_child_count() > 0 ? get_child(0) : Ref<BehaviorTask>();
	if (child.is_null()) {
		return FAILURE;
	}
	const Status result = child->execute(p_delta);
	if (result == RUNNING) {
		return RUNNING;
	}
	return result == SUCCESS ? FAILURE : SUCCESS;
}

void BehaviorInverter::_bind_methods() {
}
