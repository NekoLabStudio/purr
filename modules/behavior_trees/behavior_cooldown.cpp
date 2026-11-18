#include "behavior_cooldown.h"

#include "core/object/class_db.h"

// The tree's time, so the cooldown passes while other tasks run, and not while the game is
// paused.
BehaviorTask::Status BehaviorCooldown::tick(double p_delta) {
	if (get_tree_time() < ready_at) {
		return FAILURE;
	}
	const Ref<BehaviorTask> child = get_child_count() > 0 ? get_child(0) : Ref<BehaviorTask>();
	if (child.is_null()) {
		return FAILURE;
	}
	const Status result = child->execute(p_delta);
	if (result != RUNNING) {
		ready_at = get_tree_time() + seconds;
	}
	return result;
}

String BehaviorCooldown::describe() const {
	return vformat("Cooldown %s s", String::num(seconds, 2));
}

void BehaviorCooldown::set_seconds(double p_seconds) {
	seconds = MAX(p_seconds, 0.0);
	emit_changed();
}

double BehaviorCooldown::get_seconds() const {
	return seconds;
}

void BehaviorCooldown::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_seconds", "seconds"), &BehaviorCooldown::set_seconds);
	ClassDB::bind_method(D_METHOD("get_seconds"), &BehaviorCooldown::get_seconds);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "seconds", PROPERTY_HINT_RANGE, "0,60,0.01,or_greater,suffix:s"), "set_seconds", "get_seconds");
}
