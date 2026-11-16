#pragma once

// DynamicResolution: draws the game's 3D world at whatever share of its size holds the frame rate,
// changing as the scene gets heavier or lighter, and lets the upscaler make up the difference.
// Off unless the game turns it on.

#include "core/object/object.h"
#include "scene/main/viewport.h"

class DynamicResolution : public Object {
	GDCLASS(DynamicResolution, Object);

	static DynamicResolution *singleton;

	bool enabled = false;
	float target_fps = 0.0; // 0: the headset's or display's refresh rate.
	float min_scale = 0.5;
	float max_scale = 1.0;
	ObjectID viewport_id; // Unset: the main viewport.

	// While running.
	bool running = false;
	bool paused = false;
	float scale = 1.0;
	Viewport::Scaling3DMode original_mode = Viewport::SCALING_3D_MODE_BILINEAR;
	float original_scale = 1.0;
	double smoothed_gpu_ms = 0.0;
	double headroom = 0.0; // The share of the frame the 3D view aims to take on the GPU.
	int frames_since_check = 0;
	int frames_since_change = 0;
	int late_frames = 0;
	double calm_seconds = 0.0;
	int checks_with_room = 0;
	uint64_t last_frame_usec = 0;
	double budget_ms = 16.7; // A frame's time at the target, worked out at each check.

	Viewport *_viewport() const;
	void _start();
	void _stop();
	void _frame();
	void _set_scale(float p_scale);
	float _floor() const;

protected:
	static void _bind_methods();

public:
	static DynamicResolution *get_singleton() { return singleton; }

	void set_viewport(Viewport *p_viewport);
	Viewport *get_viewport() const;
	void set_enabled(bool p_enabled);
	bool is_enabled() const;
	void set_target_fps(float p_fps);
	float get_target_fps() const;
	void set_min_scale(float p_scale);
	float get_min_scale() const;
	void set_max_scale(float p_scale);
	float get_max_scale() const;
	float get_scale() const;
	float get_frame_rate_target() const;

	// The graphics benchmark measures fixed settings, so it holds dynamic resolution while it runs.
	void set_paused(bool p_paused);

	DynamicResolution();
	~DynamicResolution();
};
