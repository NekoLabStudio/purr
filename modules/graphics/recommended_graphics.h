#pragma once

// RecommendedGraphics: graphics settings recommended for the computer the game runs on. A tier
// ("ultra", "high", "medium", "low") comes from the graphics adapter; each tier has engine
// settings for the screen and for a headset; and a benchmark steps down the tiers until one
// holds a frame rate. Games add their own settings per tier on top.

#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "scene/main/viewport.h"

class RecommendedGraphics : public Object {
	GDCLASS(RecommendedGraphics, Object);

	static RecommendedGraphics *singleton;

	Ref<RefCounted> benchmark_running;

protected:
	static void _bind_methods();

public:
	enum Tier {
		TIER_ULTRA,
		TIER_HIGH,
		TIER_MEDIUM,
		TIER_LOW,
	};

	static RecommendedGraphics *get_singleton() { return singleton; }

	Tier get_tier() const;
	Dictionary get_settings(Tier p_tier, bool p_headset = false) const;
	void apply(Viewport *p_viewport, const Dictionary &p_settings) const;
	Signal benchmark(const Callable &p_apply_tier, float p_target_fps);

	void benchmark_finished();
	bool is_benchmarking() const { return benchmark_running.is_valid(); }

	// The upscaler for a picture drawn down to p_min_scale of its size: temporal where it's
	// smoothed with TAA (Forward+), spatial otherwise.
	static Viewport::Scaling3DMode upscaler_for(bool p_taa, float p_min_scale);

	RecommendedGraphics();
	~RecommendedGraphics();
};

VARIANT_ENUM_CAST(RecommendedGraphics::Tier);
