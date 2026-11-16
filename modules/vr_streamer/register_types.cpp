#include "register_types.h"

#include "vr.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "editor/export/editor_export.h"
#include "editor/vr_streamer_export_plugin.h"

static void add_export_plugin() {
	Ref<VRStreamerExportPlugin> plugin;
	plugin.instantiate();
	EditorExport::get_singleton()->add_export_plugin(plugin);
}
#endif

static VR *vr = nullptr;

void initialize_vr_streamer_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_CORE) {
		// Before OpenXR starts, with the servers.
		VR::start_up();
	} else if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(VR);
		vr = memnew(VR);
		Engine::get_singleton()->add_singleton(Engine::Singleton("VR", VR::get_singleton()));
	}
#ifdef TOOLS_ENABLED
	else if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorNode::add_init_callback(add_export_plugin);
	}
#endif
}

void uninitialize_vr_streamer_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE && vr) {
		memdelete(vr);
		vr = nullptr;
	}
}
