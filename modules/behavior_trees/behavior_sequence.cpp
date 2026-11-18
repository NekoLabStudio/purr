#include "behavior_sequence.h"

#include "core/object/class_db.h"

BehaviorTask::Status BehaviorSequence::tick(double p_delta) {
	while (current < get_child_count()) {
		const Ref<BehaviorTask> child = get_child(current);
		const Status result = child.is_valid() ? child->execute(p_delta) : SUCCESS;
		if (result != SUCCESS) {
			return result;
		}
		current++;
	}
	return SUCCESS;
}

void BehaviorSequence::_bind_methods() {
}
