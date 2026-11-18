// Copyright © 2023-2026 Cory Petkovsek, Roope Palmroos, and Contributors.

#ifdef GDEXTENSION
#endif

#include "core/object/class_db.h"

#include "register_types.h"
#include "terrain_3d.h"
#include "terrain_3d_editor.h"

#ifdef TOOLS_ENABLED
#include "editor/plugins/editor_plugin.h"
#include "editor/terrain_editor_plugin.h"
#include "editor/terrain_import_dialog.h"
#include "editor/terrain_texture_packer.h"
#endif

void initialize_terrain_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_INTERNAL_CLASS(TerrainImportDialog);
		Terrain3DTextureAsset::pack_hook = &TerrainTexturePacker::pack;
		GDREGISTER_INTERNAL_CLASS(TerrainEditorPlugin);
		EditorPlugins::add_by_type<TerrainEditorPlugin>();
		return;
	}
#endif
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	ClassDB::register_class<Terrain3D>();
#ifndef NAVIGATION_3D_DISABLED
	Terrain3D::navmesh_parse_init();
#endif
	ClassDB::register_abstract_class<Terrain3DAssetResource>();
	ClassDB::register_class<Terrain3DAssets>();
	ClassDB::register_class<Terrain3DData>();
	ClassDB::register_internal_class<Terrain3DEditor>(); // The editor plugin's, not for games.
	ClassDB::register_class<Terrain3DCollision>();
	ClassDB::register_class<Terrain3DInstancer>();
	ClassDB::register_class<Terrain3DMaterial>();
	ClassDB::register_class<Terrain3DMeshAsset>();
	ClassDB::register_class<Terrain3DRegion>();
	ClassDB::register_class<Terrain3DTextureAsset>();
	ClassDB::register_class<Terrain3DUtil>();
}

void uninitialize_terrain_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}

#ifdef GDEXTENSION
extern "C" {
// Initialization.
GDExtensionBool GDE_EXPORT terrain_3d_init(
		GDExtensionInterfaceGetProcAddress p_get_proc_address,
		GDExtensionClassLibraryPtr p_library,
		GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_initializer(initialize_terrain_module);
	init_obj.register_terminator(uninitialize_terrain_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SERVERS);

	return init_obj.init();
}
}
#endif /* GDEXTENSION */
