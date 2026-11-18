#pragma once

// TestMeasure: what TestCase.measure() returns a signal of. It calls a step once a frame, times
// each call (the CPU's time in it alone, not the frame's), and when done prints the median, the
// time 95 calls in 100 come within and the slowest, against the last run's on this computer.
// Kept alive by its test until then.

#include "core/object/ref_counted.h"
#include "core/variant/callable.h"

class TestMeasure : public RefCounted {
	GDCLASS(TestMeasure, RefCounted);

	String what;
	String key; // The test script's path and what, in the file of earlier runs.
	Callable step;
	int frames_left = 0;
	Vector<uint64_t> times_usec;
	bool finished = false;

	static String _path();
	void _frame();
	void _finish();

protected:
	static void _bind_methods();

public:
	void start(const String &p_what, const String &p_script, const Callable &p_step, int p_frames);
	bool is_finished() const { return finished; }
	Signal get_done() { return Signal(this, SNAME("done")); }
};
