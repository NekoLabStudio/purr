#include "behavior_parallel.h"

#include "core/object/class_db.h"

void BehaviorParallel::on_enter() {
	results.resize(get_child_count());
	results.fill(RUNNING);
}

BehaviorTask::Status BehaviorParallel::tick(double p_delta) {
	if (results.size() != get_child_count()) {
		on_enter();
	}
	bool all_succeeded = true;
	bool any_running = false;
	for (int i = 0; i < get_child_count(); i++) {
		if (results[i] == RUNNING) {
			const Ref<BehaviorTask> child = get_child(i);
			results.write[i] = child.is_valid() ? child->execute(p_delta) : SUCCESS;
			if (results[i] != RUNNING && finish_when == FINISH_WHEN_ANY) {
				for (int j = 0; j < get_child_count(); j++) {
					const Ref<BehaviorTask> other = get_child(j);
					if (j != i && other.is_valid()) {
						other->abort();
					}
				}
				return results[i];
			}
		}
		any_running = any_running || results[i] == RUNNING;
		all_succeeded = all_succeeded && results[i] != FAILURE;
	}
	if (any_running) {
		return RUNNING;
	}
	return all_succeeded ? SUCCESS : FAILURE;
}

void BehaviorParallel::set_finish_when(FinishWhen p_when) {
	finish_when = p_when;
	emit_changed();
}

BehaviorParallel::FinishWhen BehaviorParallel::get_finish_when() const {
	return finish_when;
}

void BehaviorParallel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_finish_when", "when"), &BehaviorParallel::set_finish_when);
	ClassDB::bind_method(D_METHOD("get_finish_when"), &BehaviorParallel::get_finish_when);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "finish_when", PROPERTY_HINT_ENUM, "Any,All"), "set_finish_when", "get_finish_when");
	BIND_ENUM_CONSTANT(FINISH_WHEN_ANY);
	BIND_ENUM_CONSTANT(FINISH_WHEN_ALL);
}
