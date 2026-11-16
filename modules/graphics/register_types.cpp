#include "register_types.h"

#include "dynamic_resolution.h"
#include "recommended_graphics.h"
#include "recommended_graphics_benchmark.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

static RecommendedGraphics *recommended_graphics = nullptr;
static DynamicResolution *dynamic_resolution = nullptr;

void initialize_graphics_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(RecommendedGraphics);
		GDREGISTER_INTERNAL_CLASS(RecommendedGraphicsBenchmark);
		recommended_graphics = memnew(RecommendedGraphics);
		Engine::get_singleton()->add_singleton(Engine::Singleton("RecommendedGraphics", RecommendedGraphics::get_singleton()));
		GDREGISTER_CLASS(DynamicResolution);
		dynamic_resolution = memnew(DynamicResolution);
		Engine::get_singleton()->add_singleton(Engine::Singleton("DynamicResolution", DynamicResolution::get_singleton()));
	}
}

void uninitialize_graphics_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		memdelete(dynamic_resolution);
		dynamic_resolution = nullptr;
		memdelete(recommended_graphics);
		recommended_graphics = nullptr;
	}
}
