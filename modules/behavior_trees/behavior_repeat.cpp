#include "behavior_repeat.h"

#include "core/object/class_db.h"

BehaviorTask::Status BehaviorRepeat::tick(double p_delta) {
	const Ref<BehaviorTask> child = get_child_count() > 0 ? get_child(0) : Ref<BehaviorTask>();
	if (child.is_null()) {
		return FAILURE;
	}
	const Status result = child->execute(p_delta);
	if (result == RUNNING) {
		return RUNNING;
	}
	if (result == FAILURE && !ignore_failure) {
		return FAILURE;
	}
	done++;
	return times > 0 && done >= times ? SUCCESS : RUNNING;
}

String BehaviorRepeat::describe() const {
	return times > 0 ? vformat("Repeat %d times", times) : String("Repeat for ever");
}

void BehaviorRepeat::set_times(int p_times) {
	times = MAX(p_times, 0);
	emit_changed();
}

int BehaviorRepeat::get_times() const {
	return times;
}

void BehaviorRepeat::set_ignore_failure(bool p_ignore) {
	ignore_failure = p_ignore;
	emit_changed();
}

bool BehaviorRepeat::is_ignoring_failure() const {
	return ignore_failure;
}

void BehaviorRepeat::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_times", "times"), &BehaviorRepeat::set_times);
	ClassDB::bind_method(D_METHOD("get_times"), &BehaviorRepeat::get_times);
	ClassDB::bind_method(D_METHOD("set_ignore_failure", "ignore"), &BehaviorRepeat::set_ignore_failure);
	ClassDB::bind_method(D_METHOD("is_ignoring_failure"), &BehaviorRepeat::is_ignoring_failure);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "times", PROPERTY_HINT_RANGE, "0,100,1,or_greater"), "set_times", "get_times");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ignore_failure"), "set_ignore_failure", "is_ignoring_failure");
}
