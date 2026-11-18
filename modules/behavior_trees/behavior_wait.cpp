#include "behavior_wait.h"

#include "core/object/class_db.h"

BehaviorTask::Status BehaviorWait::tick(double p_delta) {
	return get_elapsed_time() + p_delta >= seconds ? SUCCESS : RUNNING;
}

String BehaviorWait::describe() const {
	return vformat("Wait %s s", String::num(seconds, 2));
}

void BehaviorWait::set_seconds(double p_seconds) {
	seconds = MAX(p_seconds, 0.0);
	emit_changed();
}

double BehaviorWait::get_seconds() const {
	return seconds;
}

void BehaviorWait::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_seconds", "seconds"), &BehaviorWait::set_seconds);
	ClassDB::bind_method(D_METHOD("get_seconds"), &BehaviorWait::get_seconds);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "seconds", PROPERTY_HINT_RANGE, "0,60,0.01,or_greater,suffix:s"), "set_seconds", "get_seconds");
}
