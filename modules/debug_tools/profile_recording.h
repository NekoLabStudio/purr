#pragma once

// ProfileRecording: the console's profile command. For a number of seconds it records every
// frame's length, the longest frames with what FrameBudget measured in them, and (in debug builds)
// the time spent in each script function; then it saves a report to user://profiles and prints
// a summary. For a tester's machine, a device or a headset, where the editor's profiler isn't.

#include "core/object/object.h"
#include "core/templates/pair.h"
#include "core/variant/dictionary.h"

class ProfileRecording : public Object {
	GDCLASS(ProfileRecording, Object);

	static constexpr int LONGEST_KEPT = 5;
	static constexpr int FUNCTIONS_SHOWN = 30;

	uint64_t started_usec = 0;
	uint64_t last_frame_usec = 0;
	double seconds = 0.0;
	bool recording = false;
	Vector<float> frame_ms;
	Vector<Pair<double, Dictionary>> longest; // Longest first: the frame's ms, and its measured work.

	void _frame();
	void _long_frame(double p_ms, const Dictionary &p_work);
	void _finish();

public:
	bool is_recording() const { return recording; }
	double get_seconds_left() const;
	void start(double p_seconds);
	void stop();

	~ProfileRecording();
};
