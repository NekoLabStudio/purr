#include "dynamic_resolution.h"

#include "frame_pacing.h"
#include "gpu_time.h"
#include "recommended_graphics.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server.h"
#include "servers/xr/xr_interface.h"
#include "servers/xr/xr_server.h"

DynamicResolution *DynamicResolution::singleton = nullptr;

namespace {
// Scale moves in steps this size: each change configures the render buffers again, so tiny
// ones aren't worth it.
const float STEP = 0.05;
// How often it looks at the timings (frames), and the GPU time's smoothing.
const int CHECK_FRAMES = 15;
const double SMOOTHING = 0.15;
// The share of a frame the 3D view aims to take on the GPU, leaving room for the rest (2D, other
// views, spikes). It leaves more room when frames come late with the 3D view within its share.
const double HEADROOM = 0.85;
const double MIN_HEADROOM = 0.5;
const double HEADROOM_STEP = 0.05;
const double CALM_SECONDS = 10.0; // Without late frames, room left is given back after this.
const double LATE = 1.5; // A frame is late at this many times the budget.
const int LATE_IN_A_CHECK = 3;
const double BUSY_GPU = 0.4; // Late frames count against the GPU when the 3D view takes this much.
const int CHECKS_BEFORE_RISING = 2; // Scale only rises after this many checks with room, a step at a time.
// The frame rate held on a screen unless the game sets one (or vertical sync is off).
const float DEFAULT_TARGET_FPS = 60.0;
} // namespace

Viewport *DynamicResolution::_viewport() const {
	if (viewport_id.is_valid()) {
		return ObjectDB::get_instance<Viewport>(viewport_id); // Gone, it's nothing: never the main viewport instead.
	}
	SceneTree *tree = SceneTree::get_singleton();
	return tree == nullptr ? nullptr : tree->get_root();
}

void DynamicResolution::set_viewport(Viewport *p_viewport) {
	// The viewport it was changing gets its own upscaler and scale back.
	const bool was_running = running;
	_stop();
	viewport_id = p_viewport != nullptr ? p_viewport->get_instance_id() : ObjectID();
	if (was_running) {
		_start();
	}
}

Viewport *DynamicResolution::get_viewport() const {
	return ObjectDB::get_instance<Viewport>(viewport_id);
}

float DynamicResolution::get_frame_rate_target() const {
	if (target_fps > 0.0) {
		return target_fps;
	}
	Viewport *viewport = _viewport();
	float rate = FramePacing::refresh_rate(viewport);
	if (viewport != nullptr && viewport->is_using_xr()) {
		return rate; // The headset sets the pace.
	}
	DisplayServer *display = DisplayServer::get_singleton();
	// On a faster display, 60 at most with vertical sync on: most players would rather have the
	// picture sharp at 60 than soft at 120. Vertical sync off asks for the most frames there are.
	if (display == nullptr || display->window_get_vsync_mode(DisplayServerEnums::MAIN_WINDOW_ID) != DisplayServerEnums::VSYNC_DISABLED) {
		rate = MIN(rate, DEFAULT_TARGET_FPS);
	}
	// A cap of the game's own counts; FramePacing's doesn't: it caps because the full rate isn't held.
	int max_fps = Engine::get_singleton()->get_max_fps();
	if (FramePacing::get_singleton() != nullptr && max_fps == FramePacing::get_singleton()->get_own_cap()) {
		max_fps = 0;
	}
	return max_fps > 0 ? MIN(rate, float(max_fps)) : rate;
}

bool DynamicResolution::can_lower_resolution() const {
	return running && !paused && scale > _floor() + 0.001f;
}

// The lowest scale allowed: min_scale, or MetalFX's temporal upscaler's own limit if higher.
float DynamicResolution::_floor() const {
	float floor = min_scale;
	Viewport *viewport = _viewport();
	RenderingDevice *device = RenderingDevice::get_singleton();
	if (viewport != nullptr && device != nullptr && viewport->get_scaling_3d_mode() == Viewport::SCALING_3D_MODE_METALFX_TEMPORAL) {
		floor = MAX(floor, float(device->limit_get(RenderingDevice::LIMIT_METALFX_TEMPORAL_SCALER_MIN_SCALE)) / 1000000.0f);
	}
	return MIN(floor, max_scale);
}

void DynamicResolution::_start() {
	Viewport *viewport = _viewport();
	if (running || viewport == nullptr) {
		return;
	}
	running = true;
	original_mode = viewport->get_scaling_3d_mode();
	original_scale = viewport->get_scaling_3d_scale();
	// Below full size it needs an upscaler: the platform's, if the game hasn't chosen one.
	if (original_mode == Viewport::SCALING_3D_MODE_BILINEAR || original_mode == Viewport::SCALING_3D_MODE_NEAREST) {
		bool taa = viewport->is_using_taa() && !viewport->is_using_xr() && OS::get_singleton()->get_current_rendering_method() == "forward_plus";
		viewport->set_scaling_3d_mode(RecommendedGraphics::upscaler_for(taa, min_scale));
	}
	RenderingServer::get_singleton()->viewport_set_measure_render_time(viewport->get_viewport_rid(), true);
	headroom = HEADROOM;
	calm_seconds = 0.0;
	late_frames = 0;
	checks_with_room = 0;
	frames_since_check = 0;
	last_frame_usec = OS::get_singleton()->get_ticks_usec();
	budget_ms = 1000.0 / get_frame_rate_target();
	_set_scale(max_scale);
	SceneTree::get_singleton()->connect(SNAME("process_frame"), callable_mp(this, &DynamicResolution::_frame));
}

void DynamicResolution::_stop() {
	if (!running) {
		return;
	}
	running = false;
	SceneTree::get_singleton()->disconnect(SNAME("process_frame"), callable_mp(this, &DynamicResolution::_frame));
	Viewport *viewport = _viewport();
	if (viewport != nullptr) {
		RenderingServer::get_singleton()->viewport_set_measure_render_time(viewport->get_viewport_rid(), false);
		viewport->set_scaling_3d_mode(original_mode);
		viewport->set_scaling_3d_scale(original_scale);
	}
}

void DynamicResolution::_set_scale(float p_scale) {
	scale = CLAMP(p_scale, _floor(), max_scale);
	Viewport *viewport = _viewport();
	if (viewport != nullptr && viewport->get_scaling_3d_scale() != scale) {
		viewport->set_scaling_3d_scale(scale);
	}
	frames_since_change = 0;
	smoothed_gpu_ms = 0.0; // Measured again at the new scale.
}

void DynamicResolution::_frame() {
	uint64_t now = OS::get_singleton()->get_ticks_usec();
	double frame_ms = double(now - last_frame_usec) / 1000.0;
	last_frame_usec = now;
	Viewport *viewport = _viewport();
	if (viewport == nullptr) {
		_stop(); // Its viewport's gone.
		return;
	}
	if (paused) {
		return;
	}

	// The GPU's timings arrive a couple of frames late: those just after a change are the old scale's.
	frames_since_change++;
	double gpu_ms = GpuTime::frame_ms(viewport);
	if (frames_since_change > 3 && gpu_ms > 0.0) {
		smoothed_gpu_ms = smoothed_gpu_ms == 0.0 ? gpu_ms : Math::lerp(smoothed_gpu_ms, gpu_ms, SMOOTHING);
	}
	if (frame_ms > budget_ms * LATE) {
		late_frames++;
		calm_seconds = 0.0;
	} else {
		calm_seconds += frame_ms / 1000.0;
	}

	if (++frames_since_check < CHECK_FRAMES) {
		return;
	}
	frames_since_check = 0;
	// (Asked for here, not every frame: the display's refresh rate is a system call.)
	budget_ms = 1000.0 / get_frame_rate_target();
	const int late = late_frames;
	late_frames = 0;
	if (frames_since_change < CHECK_FRAMES || smoothed_gpu_ms <= 0.0) {
		return;
	}

	// Late frames while the 3D view keeps within its share, with the GPU busy: other work on the
	// GPU (other views, effects) needs more room. Given back once frames are on time for a while.
	if (late >= LATE_IN_A_CHECK && smoothed_gpu_ms >= budget_ms * BUSY_GPU && smoothed_gpu_ms <= budget_ms * headroom) {
		headroom = MAX(MIN_HEADROOM, headroom - HEADROOM_STEP);
	} else if (calm_seconds >= CALM_SECONDS && headroom < HEADROOM) {
		headroom = MIN(HEADROOM, headroom + HEADROOM_STEP);
		calm_seconds = 0.0;
	}

	// The GPU's work grows with the pixels drawn, the square of the scale.
	const float ideal = scale * Math::sqrt(budget_ms * headroom / smoothed_gpu_ms);
	if (ideal < scale - STEP * 0.5f) {
		// Down at once, to the step at or below what fits.
		_set_scale(Math::floor(ideal / STEP) * STEP);
		checks_with_room = 0;
	} else if (ideal >= scale + STEP && scale < max_scale) {
		// Up slowly, a step at a time, so a moment's lull doesn't cost a stutter.
		if (++checks_with_room >= CHECKS_BEFORE_RISING) {
			_set_scale(scale + STEP);
			checks_with_room = 0;
		}
	} else {
		checks_with_room = 0;
	}
}

void DynamicResolution::set_enabled(bool p_enabled) {
	enabled = p_enabled;
	if (enabled && !paused) {
		_start();
	} else {
		_stop();
	}
}

bool DynamicResolution::is_enabled() const {
	return enabled;
}

void DynamicResolution::set_paused(bool p_paused) {
	paused = p_paused;
	if (paused) {
		_stop();
	} else if (enabled) {
		_start();
	}
}

void DynamicResolution::set_target_fps(float p_fps) {
	target_fps = MAX(p_fps, 0.0f);
}

float DynamicResolution::get_target_fps() const {
	return target_fps;
}

void DynamicResolution::set_min_scale(float p_scale) {
	min_scale = CLAMP(p_scale, 0.25f, 1.0f);
	max_scale = MAX(max_scale, min_scale);
	if (running) {
		_set_scale(scale);
	}
}

float DynamicResolution::get_min_scale() const {
	return min_scale;
}

void DynamicResolution::set_max_scale(float p_scale) {
	max_scale = CLAMP(p_scale, 0.25f, 2.0f);
	min_scale = MIN(min_scale, max_scale);
	if (running) {
		_set_scale(scale);
	}
}

float DynamicResolution::get_max_scale() const {
	return max_scale;
}

float DynamicResolution::get_scale() const {
	if (running) {
		return scale;
	}
	Viewport *viewport = _viewport();
	return viewport != nullptr ? viewport->get_scaling_3d_scale() : 1.0f;
}

void DynamicResolution::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_viewport", "viewport"), &DynamicResolution::set_viewport);
	ClassDB::bind_method(D_METHOD("get_viewport"), &DynamicResolution::get_viewport);
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &DynamicResolution::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &DynamicResolution::is_enabled);
	ClassDB::bind_method(D_METHOD("set_target_fps", "fps"), &DynamicResolution::set_target_fps);
	ClassDB::bind_method(D_METHOD("get_target_fps"), &DynamicResolution::get_target_fps);
	ClassDB::bind_method(D_METHOD("set_min_scale", "scale"), &DynamicResolution::set_min_scale);
	ClassDB::bind_method(D_METHOD("get_min_scale"), &DynamicResolution::get_min_scale);
	ClassDB::bind_method(D_METHOD("set_max_scale", "scale"), &DynamicResolution::set_max_scale);
	ClassDB::bind_method(D_METHOD("get_max_scale"), &DynamicResolution::get_max_scale);
	ClassDB::bind_method(D_METHOD("get_scale"), &DynamicResolution::get_scale);
	ClassDB::bind_method(D_METHOD("get_frame_rate_target"), &DynamicResolution::get_frame_rate_target);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "viewport", PROPERTY_HINT_RESOURCE_TYPE, "Viewport", PROPERTY_USAGE_NONE), "set_viewport", "get_viewport");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "target_fps", PROPERTY_HINT_RANGE, "0,240,1,or_greater"), "set_target_fps", "get_target_fps");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_scale", PROPERTY_HINT_RANGE, "0.25,1,0.01"), "set_min_scale", "get_min_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_scale", PROPERTY_HINT_RANGE, "0.25,2,0.01"), "set_max_scale", "get_max_scale");
}

DynamicResolution::DynamicResolution() {
	singleton = this;
}

DynamicResolution::~DynamicResolution() {
	singleton = nullptr;
}
