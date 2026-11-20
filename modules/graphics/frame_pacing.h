#pragma once

// FramePacing: with vertical sync on, a game that can't keep up with the screen shows some frames
// for one refresh and some for two, and moves unevenly. FramePacing caps the frame rate to the
// highest even share of the refresh rate the game holds (60, then 30 on a 60 Hz screen; 120, 60,
// 40 on a 120 Hz one), so every frame is shown as long as the last, and tries a faster rate again
// when there's room. In a headset it caps only to 72 frames a second or more, leaving slower games
// to the headset's own reprojection. On by default (display/window/vsync/even_frame_pacing);
// a game that sets Engine.max_fps itself is left alone.

#include "core/object/object.h"

class Viewport;

class FramePacing : public Object {
	GDCLASS(FramePacing, Object);

	static FramePacing *singleton;

	int divisor = 1; // Frames shown for this many refreshes each: 1 is uncapped.
	int own_cap = 0; // The Engine.max_fps it set, so it knows a game's own from its.
	int frames = 0;
	int late = 0; // Frames in this check that took longer than the cap allows.
	int late_checks = 0; // Checks in a row with too many late frames.
	double work_ms = 0.0; // The frame's own cost (CPU or GPU, whichever is more), smoothed.
	double roomy_seconds = 0.0; // How long the work has fitted a faster rate.
	double wait_seconds = 0.0; // Before trying a faster rate again, after one failed.
	double backoff_seconds = 10.0;
	int trial_frames = -1; // Counting down while it tries a faster rate; -1 when not.
	uint64_t last_frame_usec = 0;
	uint64_t process_began_usec = 0; // When this frame's processing began, for its own CPU time.

	void _process_began();
	void _frame();
	bool _applies(Viewport *p_viewport, float &r_refresh) const;
	void _set_divisor(int p_divisor, float p_refresh);
	void _release();

public:
	static FramePacing *get_singleton() { return singleton; }
	// The cap it has set (0 for none), which DynamicResolution doesn't count as the game's.
	int get_own_cap() const { return own_cap; }
	// The screen's or the headset's refresh rate.
	static float refresh_rate(Viewport *p_viewport);

	FramePacing();
	~FramePacing();
};
