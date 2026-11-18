#include "test_wait.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/main/scene_tree.h"

void TestWait::start_frames(int p_count, bool p_physics) {
	frames_left = MAX(p_count, 1);
	physics = p_physics;
	SceneTree::get_singleton()->connect(physics ? SNAME("physics_frame") : SNAME("process_frame"), callable_mp(this, &TestWait::_frame));
}

void TestWait::_frame() {
	frames_left--;
	if (frames_left <= 0) {
		SceneTree::get_singleton()->disconnect(physics ? SNAME("physics_frame") : SNAME("process_frame"), callable_mp(this, &TestWait::_frame));
		finish(true);
	}
}

void TestWait::start_seconds(double p_seconds) {
	SceneTree::get_singleton()->create_timer(p_seconds)->connect(SNAME("timeout"), callable_mp(this, &TestWait::finish).bind(true), CONNECT_ONE_SHOT);
}

void TestWait::start_signal(const Signal &p_signal, double p_timeout) {
	Signal signal = p_signal;
	signal.connect(Callable(this, SNAME("_signalled")), CONNECT_ONE_SHOT);
	SceneTree::get_singleton()->create_timer(p_timeout)->connect(SNAME("timeout"), callable_mp(this, &TestWait::_timed_out), CONNECT_ONE_SHOT);
}

Variant TestWait::_signalled(const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	r_error.error = Callable::CallError::CALL_OK;
	finish(true);
	return Variant();
}

void TestWait::_timed_out() {
	finish(false);
}

// Emitted once the current call is over, so a wait that's already finished when it's awaited
// still wakes the await.
void TestWait::finish(bool p_result) {
	if (finished) {
		return;
	}
	finished = true;
	call_deferred(SNAME("emit_signal"), SNAME("done"), p_result);
}

void TestWait::_bind_methods() {
	ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "_signalled", &TestWait::_signalled, MethodInfo("_signalled"));
	ADD_SIGNAL(MethodInfo("done", PropertyInfo(Variant::BOOL, "result")));
}
