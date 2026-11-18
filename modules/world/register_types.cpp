#include "register_types.h"

#include "instance_group_3d.h"
#include "light_pool_3d.h"
#include "multimesh_body_3d.h"
#include "reflection_probe_pool.h"
#include "scatter_3d.h"
#include "time_of_day.h"
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
		GDREGISTER_CLASS(TimeOfDay);
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
		memdelete(world_origin);
		world_origin = nullptr;
	}
}
