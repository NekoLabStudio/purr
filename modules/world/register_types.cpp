#include "register_types.h"

#include "light_pool_3d.h"
#include "multimesh_body_3d.h"
#include "reflection_probe_pool.h"
#include "world_origin.h"
#include "world_streamer.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

static WorldOrigin *world_origin = nullptr;

void initialize_world_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(WorldOrigin);
		world_origin = memnew(WorldOrigin);
		Engine::get_singleton()->add_singleton(Engine::Singleton("WorldOrigin", WorldOrigin::get_singleton()));
		GDREGISTER_CLASS(WorldStreamer);
		GDREGISTER_CLASS(MultiMeshBody3D);
		GDREGISTER_CLASS(LightPool3D);
		GDREGISTER_CLASS(ReflectionProbePool);
	}
}

void uninitialize_world_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		memdelete(world_origin);
		world_origin = nullptr;
	}
}
