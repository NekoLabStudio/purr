#pragma once

// TestWait: what TestCase's wait_* methods return a signal of. It emits done once, with true (or,
// for wait_for, whether the signal came before the timeout), and is kept alive by its test until
// then.

#include "core/object/ref_counted.h"

class TestWait : public RefCounted {
	GDCLASS(TestWait, RefCounted);

	int frames_left = 0;
	bool physics = false;
	bool finished = false;

	void _frame();
	void _timed_out();
	Variant _signalled(const Variant **p_args, int p_argcount, Callable::CallError &r_error);

protected:
	static void _bind_methods();

public:
	void start_frames(int p_count, bool p_physics);
	void start_seconds(double p_seconds);
	void start_signal(const Signal &p_signal, double p_timeout);
	void finish(bool p_result);
	bool is_finished() const { return finished; }
	Signal get_done() { return Signal(this, SNAME("done")); }
};
