#include "recommended_graphics_benchmark.h"

#include "dynamic_resolution.h"
#include "recommended_graphics.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/templates/sort_array.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "servers/display/display_server.h"

namespace {
const double SETTLE_SECONDS = 1.5;
const double MEASURE_SECONDS = 3.0;
// A tier holds the target when nine frames in ten are drawn in time (to within 5%): an average
// would hide stutter.
const double PERCENTILE = 0.9;
const double HOLDS = 0.95;
} // namespace

void RecommendedGraphicsBenchmark::start(const Callable &p_apply_tier, double p_target) {
	tier_count = RecommendedGraphics::TIER_LOW + 1;
	apply_tier = p_apply_tier;
	target = p_target;
	// Vertical sync and a frame limit would hold a fast tier to the display's rate and hide how
	// much faster it could go, so they're off while measuring. In a headset the runtime paces the
	// frames at the headset's rate, which is what's measured, so they stay.
	SceneTree *tree = SceneTree::get_singleton();
	DisplayServer *display = DisplayServer::get_singleton();
	uncapped = display != nullptr && display->get_name() != "headless" && !tree->get_root()->is_using_xr();
	if (uncapped) {
		vsync_mode = display->window_get_vsync_mode(DisplayServerEnums::MAIN_WINDOW_ID);
		max_fps = Engine::get_singleton()->get_max_fps();
		display->window_set_vsync_mode(DisplayServerEnums::VSYNC_DISABLED, DisplayServerEnums::MAIN_WINDOW_ID);
		Engine::get_singleton()->set_max_fps(0);
	}
	// Each tier is measured as it is, without dynamic resolution changing it.
	DynamicResolution::get_singleton()->set_paused(true);
	tree->connect(SNAME("process_frame"), callable_mp(this, &RecommendedGraphicsBenchmark::_frame));
	_start_tier();
}

void RecommendedGraphicsBenchmark::_start_tier() {
	apply_tier.call(index);
	measuring = false;
	phase_start = OS::get_singleton()->get_ticks_usec();
}

void RecommendedGraphicsBenchmark::_frame() {
	if (uncapped) {
		// Kept off each frame: a game applying a tier through its own settings puts them back.
		DisplayServer *display = DisplayServer::get_singleton();
		if (display->window_get_vsync_mode(DisplayServerEnums::MAIN_WINDOW_ID) != DisplayServerEnums::VSYNC_DISABLED) {
			display->window_set_vsync_mode(DisplayServerEnums::VSYNC_DISABLED, DisplayServerEnums::MAIN_WINDOW_ID);
		}
		Engine::get_singleton()->set_max_fps(0);
	}
	uint64_t now = OS::get_singleton()->get_ticks_usec();
	double elapsed = double(now - phase_start) / 1000000.0;
	if (!measuring) {
		if (elapsed >= SETTLE_SECONDS) {
			measuring = true;
			phase_start = now;
			last_frame = now;
			frame_times.clear();
		}
		return;
	}
	frame_times.push_back(now - last_frame);
	last_frame = now;
	if (elapsed < MEASURE_SECONDS) {
		return;
	}
	SortArray<uint64_t> sorter;
	sorter.sort(frame_times.ptr(), frame_times.size());
	uint64_t slow = frame_times[MIN(uint32_t(frame_times.size() * PERCENTILE), frame_times.size() - 1)];
	double rate = 1000000.0 / double(MAX(slow, uint64_t(1)));
	measured[index] = rate;
	if (rate >= target * HOLDS || index == tier_count - 1) {
		_finish();
		return;
	}
	index++;
	_start_tier();
}

// The best tier that holds the target, else the one measured last (the lowest).
int RecommendedGraphicsBenchmark::_pick() const {
	for (const KeyValue<Variant, Variant> &entry : measured) {
		if (double(entry.value) >= target * HOLDS) {
			return entry.key;
		}
	}
	return measured.keys()[measured.size() - 1];
}

void RecommendedGraphicsBenchmark::_finish() {
	SceneTree::get_singleton()->disconnect(SNAME("process_frame"), callable_mp(this, &RecommendedGraphicsBenchmark::_frame));
	if (uncapped) {
		DisplayServer::get_singleton()->window_set_vsync_mode(DisplayServerEnums::VSyncMode(vsync_mode), DisplayServerEnums::MAIN_WINDOW_ID);
		Engine::get_singleton()->set_max_fps(max_fps);
	}
	const int chosen = _pick();
	if (chosen != index) {
		apply_tier.call(chosen);
	}
	DynamicResolution::get_singleton()->set_paused(false);
	Ref<RecommendedGraphicsBenchmark> keep = this; // The singleton lets go of it here.
	RecommendedGraphics::get_singleton()->benchmark_finished();
	emit_signal(SNAME("completed"), chosen);
}

void RecommendedGraphicsBenchmark::_bind_methods() {
	ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::INT, "tier")));
}
