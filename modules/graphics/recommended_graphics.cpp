#include "recommended_graphics.h"

#include "adapter_tier.h"
#include "recommended_graphics_benchmark.h"

#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "servers/display/display_server.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server.h"
#include "servers/xr/xr_interface.h"
#include "servers/xr/xr_server.h"

RecommendedGraphics *RecommendedGraphics::singleton = nullptr;

namespace {


struct TierSettings {
	// Upscaling: the share of the screen's size the 3D world is drawn at, and at most how many
	// pixels it draws (0: no limit), so a 4K, 5K or ultrawide screen upscales further than a 1080p
	// one and any screen size, including ones to come, falls into place.
	float scale;
	float pixel_budget;
	Viewport::MSAA msaa;
	bool taa;
	Viewport::ScreenSpaceAA screen_space_aa;
	Viewport::AnisotropicFiltering anisotropy;
	float lod_threshold;
	int shadow_size;
	// In a headset: no TAA (it smears as the head turns), MSAA instead, and a budget per eye.
	float headset_pixel_budget;
	Viewport::MSAA headset_msaa;
	int headset_shadow_size;
};

const TierSettings TIER_SETTINGS[] = {
	{ 1.0, 0, Viewport::MSAA_2X, true, Viewport::SCREEN_SPACE_AA_DISABLED, Viewport::ANISOTROPY_16X, 1.0, 8192, 0, Viewport::MSAA_4X, 8192 },
	{ 1.0, 10600000, Viewport::MSAA_DISABLED, true, Viewport::SCREEN_SPACE_AA_DISABLED, Viewport::ANISOTROPY_4X, 1.0, 8192, 0, Viewport::MSAA_2X, 4096 },
	{ 0.85, 7200000, Viewport::MSAA_DISABLED, true, Viewport::SCREEN_SPACE_AA_DISABLED, Viewport::ANISOTROPY_4X, 2.0, 4096, 4000000, Viewport::MSAA_2X, 2048 },
	{ 0.7, 3700000, Viewport::MSAA_DISABLED, false, Viewport::SCREEN_SPACE_AA_FXAA, Viewport::ANISOTROPY_2X, 4.0, 2048, 3000000, Viewport::MSAA_2X, 2048 },
};

// Below half size, upscaling looks too soft.
const float MIN_SCALE = 0.5;

float screen_pixels() {
	DisplayServer *display = DisplayServer::get_singleton();
	if (display == nullptr || display->get_name() == "headless") {
		return 0.0;
	}
	Size2i size = display->screen_get_size(display->window_get_current_screen());
	return float(size.x) * float(size.y);
}

// The pixels a headset draws for each eye, or 0 without one running.
float eye_pixels() {
	XRServer *xr = XRServer::get_singleton();
	Ref<XRInterface> interface = xr == nullptr ? Ref<XRInterface>() : xr->get_primary_interface();
	if (interface.is_null() || !interface->is_initialized()) {
		return 0.0;
	}
	Size2 size = interface->get_render_target_size();
	return size.x * size.y;
}

// The tier's scale, lowered so it draws no more than the budget.
float scale_within(float p_scale, float p_budget, float p_pixels) {
	if (p_budget > 0.0 && p_pixels > 0.0) {
		p_scale = MIN(p_scale, Math::sqrt(p_budget / p_pixels));
	}
	return CLAMP(Math::snapped(p_scale, 0.01f), MIN_SCALE, 1.0f);
}

bool supports(RenderingDevice::Features p_feature) {
	RenderingDevice *device = RenderingDevice::get_singleton();
	return device != nullptr && device->has_feature(p_feature);
}

// MetalFX's temporal upscaler only scales within limits the device sets.
bool metalfx_temporal_scales(float p_scale) {
	if (!supports(RenderingDevice::SUPPORTS_METALFX_TEMPORAL)) {
		return false;
	}
	RenderingDevice *device = RenderingDevice::get_singleton();
	double min_scale = double(device->limit_get(RenderingDevice::LIMIT_METALFX_TEMPORAL_SCALER_MIN_SCALE)) / 1000000.0;
	double max_scale = double(device->limit_get(RenderingDevice::LIMIT_METALFX_TEMPORAL_SCALER_MAX_SCALE)) / 1000000.0;
	return p_scale >= min_scale && p_scale <= max_scale;
}

} // namespace

// Where the picture is smoothed with TAA, a temporal upscaler (it does TAA's work as it scales);
// otherwise a spatial one. Apple's MetalFX where the device has it, else FSR.
Viewport::Scaling3DMode RecommendedGraphics::upscaler_for(bool p_taa, float p_min_scale) {
	if (p_taa) {
		return metalfx_temporal_scales(p_min_scale) ? Viewport::SCALING_3D_MODE_METALFX_TEMPORAL : Viewport::SCALING_3D_MODE_FSR2;
	}
	return supports(RenderingDevice::SUPPORTS_METALFX_SPATIAL) ? Viewport::SCALING_3D_MODE_METALFX_SPATIAL : Viewport::SCALING_3D_MODE_FSR;
}

RecommendedGraphics::Tier RecommendedGraphics::get_tier() const {
	RenderingServer *rendering = RenderingServer::get_singleton();
	String adapter = rendering->get_video_adapter_name();
	int index = adapter_tier(adapter.utf8().get_data());
	if (index >= 0) {
		return Tier(index); // adapter_tier counts from ultra, as Tier does.
	}
	// Unknown families: integrated adapters (and phones', tablets' and Apple TV's) low, discrete
	// ones medium.
	return rendering->get_video_adapter_type() == RenderingDeviceEnums::DEVICE_TYPE_DISCRETE_GPU ? TIER_MEDIUM : TIER_LOW;
}

Dictionary RecommendedGraphics::get_settings(Tier p_tier, bool p_headset) const {
	ERR_FAIL_INDEX_V_MSG((int)p_tier, (int)std_size(TIER_SETTINGS), Dictionary(), "Unknown graphics tier: use one of RecommendedGraphics.Tier.");
	const TierSettings &tier = TIER_SETTINGS[p_tier];
	// Forward+ has TAA and the temporal upscalers; Mobile smooths with FXAA instead.
	bool forward_plus = OS::get_singleton()->get_current_rendering_method() == "forward_plus";

	float scale = p_headset ? scale_within(tier.scale, tier.headset_pixel_budget, eye_pixels()) : scale_within(tier.scale, tier.pixel_budget, screen_pixels());
	bool taa = false;
	Viewport::MSAA msaa = p_headset ? tier.headset_msaa : tier.msaa;
	Viewport::ScreenSpaceAA screen_space_aa = Viewport::SCREEN_SPACE_AA_DISABLED;
	if (!p_headset) {
		taa = tier.taa && forward_plus;
		screen_space_aa = tier.taa && !forward_plus ? Viewport::SCREEN_SPACE_AA_FXAA : tier.screen_space_aa;
	}

	Viewport::Scaling3DMode mode = scale < 1.0 ? upscaler_for(taa, scale) : Viewport::SCALING_3D_MODE_BILINEAR;

	Dictionary result;
	result["scaling_3d_mode"] = mode;
	result["scaling_3d_scale"] = scale;
	result["msaa_3d"] = msaa;
	result["screen_space_aa"] = screen_space_aa;
	result["use_taa"] = taa;
	result["anisotropic_filtering_level"] = tier.anisotropy;
	result["mesh_lod_threshold"] = tier.lod_threshold;
	result["directional_shadow_size"] = p_headset ? tier.headset_shadow_size : tier.shadow_size;
	return result;
}

void RecommendedGraphics::apply(Viewport *p_viewport, const Dictionary &p_settings) const {
	ERR_FAIL_NULL(p_viewport);
	for (const KeyValue<Variant, Variant> &entry : p_settings) {
		if (String(entry.key) == "directional_shadow_size") {
			RenderingServer::get_singleton()->directional_shadow_atlas_set_size(entry.value, true);
			continue;
		}
		bool valid = false;
		p_viewport->set(entry.key, entry.value, &valid);
		ERR_CONTINUE_MSG(!valid, vformat("\"%s\" isn't a viewport setting RecommendedGraphics can apply.", entry.key));
	}
}

Signal RecommendedGraphics::benchmark(const Callable &p_apply_tier, float p_target_fps) {
	ERR_FAIL_COND_V_MSG(benchmark_running.is_valid(), Signal(), "A graphics benchmark is already running.");
	ERR_FAIL_NULL_V_MSG(SceneTree::get_singleton(), Signal(), "The graphics benchmark needs the scene tree.");
	Ref<RecommendedGraphicsBenchmark> run;
	run.instantiate();
	benchmark_running = run;
	run->start(p_apply_tier, p_target_fps);
	return Signal(run.ptr(), SNAME("completed"));
}

void RecommendedGraphics::benchmark_finished() {
	benchmark_running.unref();
}

void RecommendedGraphics::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_tier"), &RecommendedGraphics::get_tier);
	ClassDB::bind_method(D_METHOD("get_settings", "tier", "headset"), &RecommendedGraphics::get_settings, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("apply", "viewport", "settings"), &RecommendedGraphics::apply);
	ClassDB::bind_method(D_METHOD("benchmark", "apply_tier", "target_fps"), &RecommendedGraphics::benchmark);

	BIND_ENUM_CONSTANT(TIER_ULTRA);
	BIND_ENUM_CONSTANT(TIER_HIGH);
	BIND_ENUM_CONSTANT(TIER_MEDIUM);
	BIND_ENUM_CONSTANT(TIER_LOW);
}

RecommendedGraphics::RecommendedGraphics() {
	singleton = this;
}

RecommendedGraphics::~RecommendedGraphics() {
	singleton = nullptr;
}
