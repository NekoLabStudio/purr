#pragma once

// The performance overlay: a panel in the top-right corner with the frame rate and a graph of
// recent frame times, where the CPU and GPU spend the frame (the GPU's five costliest render passes
// among them), and the draw calls, objects and video memory. Numbers are averaged over half a
// second so they can be read. DebugDraw.performance_overlay (or the console's perf) shows it.

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/pair.h"
#include "core/templates/rid.h"
#include "core/templates/vector.h"

class Viewport;

class PerformanceOverlay {
	static constexpr int HISTORY = 120; // Frames in the graph.
	static constexpr uint64_t AVERAGE_USEC = 500000;

	RID item;
	RID measured_viewport;
	bool profiling = false;
	Vector<float> frame_ms; // Oldest first.
	uint64_t last_frame_usec = 0;

	// Summed since the shown numbers last changed.
	uint64_t window_start_usec = 0;
	int window_frames = 0;
	double frame_sum = 0.0;
	int frames_summed = 0;
	double gpu_sum = 0.0;
	uint64_t last_profile_frame = 0;
	int profile_frames = 0;
	HashMap<String, double> pass_sums;

	// What's shown, as label and value.
	Vector<Pair<String, String>> lines;
	Vector<Pair<String, String>> passes;

	void _sample(Viewport *p_root, uint64_t p_now);
	void _publish();

public:
	void update(Viewport *p_root, RID p_canvas);
	void stop(bool p_running = true);

	~PerformanceOverlay();
};
