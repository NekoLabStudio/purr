#include "register_types.h"

#include "baked_animation_material.h"
#include "baked_animations.h"
#include "cloudy_sky_material.h"
#include "impostor.h"
#include "impostor_material.h"
#include "foliage_bender_3d.h"
#include "foliage_material.h"
#include "instance_group_3d.h"
#include "interior_material.h"
#include "light_pool_3d.h"
#include "multimesh_body_3d.h"
#include "reflection_probe_pool.h"
#include "scatter_3d.h"
#include "static_scenery_3d.h"
#include "time_of_day.h"
#include "ground_marks_3d.h"
#include "water_3d.h"
#include "water_material.h"
#include "weather_3d.h"
#include "world_origin.h"
#include "world_streamer.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "editor/scatter_3d_gizmo_plugin.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#endif

static WorldOrigin *world_origin = nullptr;

void initialize_world_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(WorldOrigin);
		world_origin = memnew(WorldOrigin);
		Engine::get_singleton()->add_singleton(Engine::Singleton("WorldOrigin", WorldOrigin::get_singleton()));
		GDREGISTER_CLASS(WorldStreamer);
		GDREGISTER_CLASS(MultiMeshBody3D);
		GDREGISTER_CLASS(InstanceGroup3D);
		GDREGISTER_CLASS(LightPool3D);
		GDREGISTER_CLASS(ReflectionProbePool);
		GDREGISTER_CLASS(Scatter3D);
		GDREGISTER_CLASS(StaticScenery3D);
		GDREGISTER_CLASS(TimeOfDay);
		GDREGISTER_CLASS(Weather3D);
		GDREGISTER_CLASS(CloudySkyMaterial);
		GDREGISTER_CLASS(FoliageMaterial);
		GDREGISTER_CLASS(FoliageBender3D);
		GDREGISTER_CLASS(GroundMarks3D);
		GDREGISTER_CLASS(WaterMaterial);
		GDREGISTER_CLASS(InteriorMaterial);
		GDREGISTER_CLASS(Water3D);
		GDREGISTER_CLASS(BakedAnimations);
		GDREGISTER_CLASS(BakedAnimationMaterial);
		GDREGISTER_CLASS(Impostor);
		GDREGISTER_CLASS(ImpostorMaterial);
		GDREGISTER_INTERNAL_CLASS(ImpostorBake);
	}
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorNode::add_init_callback([]() {
			Node3DEditor::get_singleton()->add_gizmo_plugin(memnew(Scatter3DGizmoPlugin));
		});
	}
#endif
}

void uninitialize_world_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		CloudySkyMaterial::cleanup_shader();
		FoliageMaterial::cleanup_shader();
		FoliageBender3D::cleanup();
		GroundMarks3D::cleanup();
		WaterMaterial::cleanup_shader();
		InteriorMaterial::cleanup_shader();
		BakedAnimationMaterial::cleanup_shader();
		ImpostorMaterial::cleanup_shader();
		memdelete(world_origin);
		world_origin = nullptr;
	}
}
