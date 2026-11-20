#include "frame_pacing.h"

#include "dynamic_resolution.h"
#include "gpu_time.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "main/performance.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_server.h"
#include "servers/xr/xr_interface.h"
#include "servers/xr/xr_server.h"

FramePacing *FramePacing::singleton = nullptr;

namespace {
const char *SETTING = "display/window/vsync/even_frame_pacing";
constexpr int CHECK_FRAMES = 60;
constexpr float LATE = 1.4; // A frame this much longer than the cap allows was shown a refresh late.
constexpr float TOO_MANY_LATE = 0.1; // The share of late frames that means the rate isn't held.
constexpr float ROOM = 0.8; // A faster rate is tried when the work fits this share of its time.
constexpr double ROOMY_SECONDS = 3.0;
constexpr int TRIAL_FRAMES = 90;
constexpr float LOWEST_RATE = 20.0; // Never capped below this: past it, evenness doesn't help.
constexpr float LOWEST_HEADSET_RATE = 72.0; // Below this, a headset's own reprojection does better.
constexpr double LONGEST_BACKOFF = 120.0;
constexpr uint64_t SETTLING_USEC = 2000000; // The first frames load and compile: not counted.
constexpr int LATE_CHECKS = 2; // A slower rate only after this many checks in a row with late frames.
} // namespace

FramePacing::FramePacing() {
	singleton = this;
	GLOBAL_DEF(SETTING, true);
	if (RenderingServer::get_singleton() != nullptr) {
		RenderingServer::get_singleton()->connect(SNAME("frame_pre_draw"), callable_mp(this, &FramePacing::_frame));
	}
}

FramePacing::~FramePacing() {
	if (RenderingServer::get_singleton() != nullptr && RenderingServer::get_singleton()->is_connected(SNAME("frame_pre_draw"), callable_mp(this, &FramePacing::_frame))) {
		RenderingServer::get_singleton()->disconnect(SNAME("frame_pre_draw"), callable_mp(this, &FramePacing::_frame));
	}
	singleton = nullptr;
}

float FramePacing::refresh_rate(Viewport *p_viewport) {
	if (p_viewport != nullptr && p_viewport->is_using_xr() && XRServer::get_singleton() != nullptr) {
		// The headset sets the pace (OpenXR reports its refresh rate).
		Ref<XRInterface> xr = XRServer::get_singleton()->get_primary_interface();
		const float rate = xr.is_valid() && xr->has_method(SNAME("get_display_refresh_rate")) ? float(xr->call(SNAME("get_display_refresh_rate"))) : 0.0f;
		return rate > 0.0f ? rate : 90.0f;
	}
	DisplayServer *display = DisplayServer::get_singleton();
	const float rate = display != nullptr ? display->screen_get_refresh_rate(display->window_get_current_screen()) : 0.0f;
	return rate > 0.0f ? rate : 60.0f;
}

// Whether it paces now: in a game, with vertical sync, the setting on and no cap of the game's own.
bool FramePacing::_applies(Viewport *p_viewport, float &r_refresh) const {
	if (Engine::get_singleton()->is_editor_hint() || p_viewport == nullptr || Performance::get_singleton() == nullptr) {
		return false;
	}
	DisplayServer *display = DisplayServer::get_singleton();
	if (display == nullptr || display->get_name() == "headless" || display->window_get_vsync_mode(DisplayServerEnums::MAIN_WINDOW_ID) == DisplayServerEnums::VSYNC_DISABLED) {
		return false;
	}
	if (!bool(GLOBAL_GET(SETTING))) {
		return false;
	}
	const int max_fps = Engine::get_singleton()->get_max_fps();
	if (max_fps != 0 && max_fps != own_cap) {
		return false; // The game's own cap.
	}
	r_refresh = refresh_rate(p_viewport);
	return true;
}

void FramePacing::_set_divisor(int p_divisor, float p_refresh) {
	divisor = MAX(p_divisor, 1);
	own_cap = divisor > 1 ? int(Math::round(p_refresh / divisor)) : 0;
	Engine::get_singleton()->set_max_fps(own_cap);
	frames = 0;
	late = 0;
	roomy_seconds = 0.0;
}

void FramePacing::_release() {
	if (own_cap != 0 && Engine::get_singleton()->get_max_fps() == own_cap) {
		Engine::get_singleton()->set_max_fps(0);
	}
	own_cap = 0;
	divisor = 1;
	trial_frames = -1;
	frames = 0;
	late = 0;
}

void FramePacing::_process_began() {
	process_began_usec = OS::get_singleton()->get_ticks_usec();
}

void FramePacing::_frame() {
	GpuTime::add_monitor(); // Here, as this runs every frame from the first.
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	const double frame_ms = last_frame_usec == 0 ? 0.0 : double(now - last_frame_usec) / 1000.0;
	last_frame_usec = now;
	SceneTree *tree = SceneTree::get_singleton();
	Viewport *viewport = tree != nullptr ? tree->get_root() : nullptr;
	float refresh = 60.0f;
	if (!_applies(viewport, refresh)) {
		if (own_cap != 0 || divisor != 1) {
			_release();
		}
		return;
	}
	// A window in the background is drawn at whatever pace the system likes, and the game's first
	// seconds are loading: neither says what the game holds.
	if (now < SETTLING_USEC || !DisplayServer::get_singleton()->window_is_focused(DisplayServerEnums::MAIN_WINDOW_ID)) {
		frames = 0;
		late = 0;
		return;
	}
	const bool headset = viewport->is_using_xr();
	const double interval_ms = 1000.0 / refresh;

	// The frame's own cost, to know when a faster rate would fit. (It only decides when to try;
	// late frames decide the rest.)
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->viewport_set_measure_render_time(viewport->get_viewport_rid(), true);
	// The game's own work: from its processing to drawing, without the wait for the cap or the
	// screen (Performance's process time includes those), with physics and the render setup.
	if (!tree->is_connected(SNAME("process_frame"), callable_mp(this, &FramePacing::_process_began))) {
		tree->connect(SNAME("process_frame"), callable_mp(this, &FramePacing::_process_began));
	}
	const double processing_ms = process_began_usec > 0 && now > process_began_usec ? double(now - process_began_usec) / 1000.0 : 0.0;
	process_began_usec = 0;
	const double cpu_ms = processing_ms + Performance::get_singleton()->get_monitor(Performance::TIME_PHYSICS_PROCESS) * 1000.0 + rs->viewport_get_measured_render_time_cpu(viewport->get_viewport_rid());
	const double gpu_ms = GpuTime::frame_ms(viewport);
	const double cost = MAX(cpu_ms, gpu_ms);
	if (cost > 0.0) {
		work_ms = work_ms <= 0.0 ? cost : Math::lerp(work_ms, cost, 0.1);
	}
	if (frame_ms > interval_ms * divisor * LATE) {
		late++;
	}
	frames++;

	if (trial_frames >= 0) {
		if (--trial_frames > 0) {
			return;
		}
		trial_frames = -1;
		if (late > frames * TOO_MANY_LATE) {
			// It didn't hold: back to the slower rate, and wait longer before trying again.
			_set_divisor(divisor + 1, refresh);
			wait_seconds = backoff_seconds;
			backoff_seconds = MIN(backoff_seconds * 2.0, LONGEST_BACKOFF);
		} else {
			backoff_seconds = 10.0;
			frames = 0;
			late = 0;
		}
		return;
	}
	if (frames < CHECK_FRAMES) {
		return;
	}
	const double window_seconds = frames * interval_ms * divisor / 1000.0;
	late_checks = late > frames * TOO_MANY_LATE ? late_checks + 1 : 0;
	if (late_checks >= LATE_CHECKS) {
		late_checks = 0;
		// Not holding its rate. While dynamic resolution can still draw fewer pixels, that's its job.
		const DynamicResolution *resolution = DynamicResolution::get_singleton();
		if (resolution == nullptr || !resolution->can_lower_resolution()) {
			const int slower = divisor + 1;
			if (refresh / slower >= (headset ? LOWEST_HEADSET_RATE : LOWEST_RATE)) {
				_set_divisor(slower, refresh);
			} else if (headset && divisor > 1) {
				_set_divisor(1, refresh); // Too slow to pace evenly in a headset: its reprojection takes over.
			}
		}
	} else if (late_checks == 0 && divisor > 1) {
		wait_seconds = MAX(wait_seconds - window_seconds, 0.0);
		if (work_ms > 0.0 && work_ms < interval_ms * (divisor - 1) * ROOM) {
			roomy_seconds += window_seconds;
		} else {
			roomy_seconds = 0.0;
		}
		if (roomy_seconds >= ROOMY_SECONDS && wait_seconds <= 0.0) {
			_set_divisor(divisor - 1, refresh);
			trial_frames = TRIAL_FRAMES;
			return;
		}
	}
	frames = 0;
	late = 0;
}
