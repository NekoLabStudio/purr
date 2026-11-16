#pragma once

// Measures graphics tiers in the game as it stands, best first: each applied, left to settle,
// then every frame timed, until one holds the target. The one chosen stays applied.
// RecommendedGraphics.benchmark() starts one.

#include "core/object/ref_counted.h"
#include "core/templates/local_vector.h"
#include "core/variant/dictionary.h"

class RecommendedGraphicsBenchmark : public RefCounted {
	GDCLASS(RecommendedGraphicsBenchmark, RefCounted);

	int tier_count = 0; // RecommendedGraphics::Tier, measured best first.
	Callable apply_tier;
	double target = 60.0;
	int index = 0;
	bool measuring = false;
	uint64_t phase_start = 0;
	uint64_t last_frame = 0;
	LocalVector<uint64_t> frame_times; // Microseconds.
	Dictionary measured; // Tier: the frames a second it holds, best first.

	// What the benchmark lifts while it runs, put back after.
	bool uncapped = false;
	int vsync_mode = 0;
	int max_fps = 0;

	void _start_tier();
	void _frame();
	void _finish();
	int _pick() const;

protected:
	static void _bind_methods();

public:
	void start(const Callable &p_apply_tier, double p_target);
};
