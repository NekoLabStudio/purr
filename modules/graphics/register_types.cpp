#include "register_types.h"

#include "color_grade.h"
#include "color_grade_volume_3d.h"
#include "dynamic_resolution.h"
#include "frame_budget.h"
#include "frame_pacing.h"
#include "recommended_graphics.h"
#include "recommended_graphics_benchmark.h"
#include "sky_globals.h"
#include "weather_snow_shader.h"

#ifdef SCREEN_EFFECTS_ENABLED
#include "effects/aerial_perspective_effect.h"
#include "effects/atmosphere_sky_material.h"
#include "effects/atmosphere_tables.h"
#include "effects/chromatic_aberration_effect.h"
#include "effects/film_grain_effect.h"
#include "effects/lens_flare_effect.h"
#include "effects/light_shafts_effect.h"
#include "effects/local_exposure_effect.h"
#include "effects/motion_blur_effect.h"
#include "effects/vignette_effect.h"
#include "effects/weather_shelter_texture.h"
#include "effects/wet_surfaces_effect.h"
#endif

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "scene/resources/material.h"

static RecommendedGraphics *recommended_graphics = nullptr;
static DynamicResolution *dynamic_resolution = nullptr;
static FrameBudget *frame_budget = nullptr;
static FramePacing *frame_pacing = nullptr;

void initialize_graphics_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		// The weather's shader globals exist from the start, for materials that read them as they load.
		SkyGlobals::ensure();
		// Snow settling on every lit StandardMaterial3D, unless the project turns it off.
		if (GLOBAL_DEF("rendering/weather/snow_on_materials", true)) {
			BaseMaterial3D::set_extra_shader_code(WEATHER_SNOW_DECLARATIONS, WEATHER_SNOW_FRAGMENT);
		}
		GDREGISTER_CLASS(RecommendedGraphics);
		GDREGISTER_INTERNAL_CLASS(RecommendedGraphicsBenchmark);
		recommended_graphics = memnew(RecommendedGraphics);
		Engine::get_singleton()->add_singleton(Engine::Singleton("RecommendedGraphics", RecommendedGraphics::get_singleton()));
		GDREGISTER_CLASS(DynamicResolution);
		dynamic_resolution = memnew(DynamicResolution);
		Engine::get_singleton()->add_singleton(Engine::Singleton("DynamicResolution", DynamicResolution::get_singleton()));
		GDREGISTER_CLASS(FrameBudget);
		GDREGISTER_CLASS(ColorGrade);
		GDREGISTER_CLASS(ColorGradeVolume3D);
		frame_budget = memnew(FrameBudget);
		Engine::get_singleton()->add_singleton(Engine::Singleton("FrameBudget", FrameBudget::get_singleton()));
		GDREGISTER_INTERNAL_CLASS(FramePacing);
		frame_pacing = memnew(FramePacing);
#ifdef SCREEN_EFFECTS_ENABLED
		GDREGISTER_CLASS(MotionBlurEffect);
		GDREGISTER_CLASS(LightShaftsEffect);
		GDREGISTER_CLASS(LocalExposureEffect);
		GDREGISTER_CLASS(LensFlareEffect);
		GDREGISTER_CLASS(VignetteEffect);
		GDREGISTER_CLASS(FilmGrainEffect);
		GDREGISTER_CLASS(ChromaticAberrationEffect);
		GDREGISTER_CLASS(AtmosphereSkyMaterial);
		GDREGISTER_CLASS(AerialPerspectiveEffect);
		GDREGISTER_CLASS(WetSurfacesEffect);
#endif
	}
}

void uninitialize_graphics_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		ColorGradeVolume3D::cleanup();
		SkyGlobals::cleanup();
#ifdef SCREEN_EFFECTS_ENABLED
		WeatherShelterTexture::cleanup();
		AtmosphereSkyMaterial::cleanup_shader();
		AtmosphereTables::free_singleton();
#endif
		memdelete(frame_pacing);
		frame_pacing = nullptr;
		memdelete(frame_budget);
		frame_budget = nullptr;
		memdelete(dynamic_resolution);
		dynamic_resolution = nullptr;
		memdelete(recommended_graphics);
		recommended_graphics = nullptr;
	}
}
