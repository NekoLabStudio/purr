#include "behavior_selector.h"

#include "core/object/class_db.h"

BehaviorTask::Status BehaviorSelector::tick(double p_delta) {
	while (current < get_child_count()) {
		const Ref<BehaviorTask> child = get_child(current);
		const Status result = child.is_valid() ? child->execute(p_delta) : FAILURE;
		if (result != FAILURE) {
			return result;
		}
		current++;
	}
	return FAILURE;
}

void BehaviorSelector::_bind_methods() {
}
