#pragma once

// FrameBudget: says what made a frame long, and spreads heavy work over frames. The game times
// named pieces of its work (measure()); a frame longer than long_frame_ms says what took its
// time. Work that can wait runs a slice at a time within a budget each frame (spread()), more of
// it while nothing's seen (screen_hidden).

#include "core/object/object.h"
#include "core/templates/hash_map.h"
#include "core/variant/callable.h"

class FrameBudget : public Object {
	GDCLASS(FrameBudget, Object);

	static FrameBudget *singleton;

	struct Job {
		String what;
		Callable step;
		double budget_ms = 0.0;
	};

	bool reporting = false;
	float long_frame_ms = 40.0;
	bool screen_hidden = false;

	HashMap<String, uint64_t> work; // This frame's measured work: what -> microseconds.
	Vector<Job> jobs;
	uint64_t last_frame_usec = 0;
	uint64_t last_report_usec = 0;
	bool started = false;

	void _start();
	void _tree_exiting();
	void _frame();
	void _run_jobs();
	void _report(double p_ms, const Dictionary &p_work);

protected:
	static void _bind_methods();

public:
	static FrameBudget *get_singleton() { return singleton; }

	// How many times its budget spread work takes while the screen's hidden.
	static constexpr double HIDDEN_TIMES = 6.0;

	void measure(const String &p_what, uint64_t p_since_usec);
	void spread(const String &p_what, const Callable &p_step, double p_budget_ms = 2.0);
	int get_spread_count() const;

	void set_reporting(bool p_reporting);
	bool is_reporting() const;
	void set_long_frame_ms(float p_ms);
	float get_long_frame_ms() const;
	void set_screen_hidden(bool p_hidden);
	bool is_screen_hidden() const;

	FrameBudget();
	~FrameBudget();
};
