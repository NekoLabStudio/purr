#include "behavior_time_limit.h"

#include "core/object/class_db.h"

BehaviorTask::Status BehaviorTimeLimit::tick(double p_delta) {
	const Ref<BehaviorTask> child = get_child_count() > 0 ? get_child(0) : Ref<BehaviorTask>();
	if (child.is_null()) {
		return FAILURE;
	}
	const Status result = child->execute(p_delta);
	if (result == RUNNING && get_elapsed_time() + p_delta >= seconds) {
		child->abort();
		return FAILURE;
	}
	return result;
}

String BehaviorTimeLimit::describe() const {
	return vformat("Time limit %s s", String::num(seconds, 2));
}

void BehaviorTimeLimit::set_seconds(double p_seconds) {
	seconds = MAX(p_seconds, 0.0);
	emit_changed();
}

double BehaviorTimeLimit::get_seconds() const {
	return seconds;
}

void BehaviorTimeLimit::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_seconds", "seconds"), &BehaviorTimeLimit::set_seconds);
	ClassDB::bind_method(D_METHOD("get_seconds"), &BehaviorTimeLimit::get_seconds);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "seconds", PROPERTY_HINT_RANGE, "0,60,0.01,or_greater,suffix:s"), "set_seconds", "get_seconds");
}
