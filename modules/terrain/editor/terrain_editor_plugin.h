#pragma once

// Editing terrain in the 3D view. With a Terrain3D selected, a row of tools appears in the 3D
// view's toolbar: add or remove regions, raise, lower, smooth or flatten the ground, paint or
// blend its textures, tint it, cut holes, and place or remove meshes (grass, rocks and the like).
// A brush circle follows the pointer; drag to use the tool. Every stroke can be undone.

#include "../terrain_3d.h"

#include "editor/plugins/editor_plugin.h"

class Button;
class ButtonGroup;
class ColorPickerButton;
class Decal;
class EditorSpinSlider;
class HBoxContainer;
class OptionButton;
class TerrainImportDialog;
class ImageTexture;

class TerrainEditorPlugin : public EditorPlugin {
	GDCLASS(TerrainEditorPlugin, EditorPlugin);

	struct ToolInfo {
		const char *name;
		const char *tooltip;
		Terrain3DEditor::Tool tool;
		Terrain3DEditor::Operation operation;
	};

	static const ToolInfo TOOLS[];

	enum MenuItem {
		MENU_IMPORT,
	};
	TerrainImportDialog *import_dialog = nullptr;
	void _menu_chosen(int p_id);

	ObjectID terrain_id;
	Terrain3DEditor *editor = nullptr;
	int current_tool = -1; // None: the 3D view works as usual.
	bool painting = false;

	HBoxContainer *toolbar = nullptr;
	Ref<ButtonGroup> tool_group;
	Vector<Button *> tool_buttons;
	EditorSpinSlider *size_slider = nullptr;
	EditorSpinSlider *strength_slider = nullptr;
	EditorSpinSlider *slope_min_slider = nullptr;
	EditorSpinSlider *slope_max_slider = nullptr;
	EditorSpinSlider *height_slider = nullptr;
	ColorPickerButton *color_picker = nullptr;
	// Picks the texture to paint or the mesh to place, from the terrain's assets.
	struct AssetPicker {
		OptionButton *list = nullptr;
		Button *edit = nullptr;
		Button *add = nullptr;
	};
	AssetPicker pickers[2]; // By Terrain3DAssets::AssetType.
	Ref<Terrain3DAssets> watched_assets; // Whose assets the pickers list.

	Dictionary brush_data;
	Ref<Image> brush_image;
	Ref<ImageTexture> brush_texture;
	ObjectID decal_id; // The brush circle, under the terrain.

	Terrain3D *_terrain() const;
	void _make_brush();
	void _tool_toggled(bool p_pressed, int p_tool);
	void _update_settings();
	void _update_brush_data();
	void _update_icons();
	void _scene_saved(const String &p_path);
	void _save_again_if_needed(const String &p_path);
	static bool _needs_data_directory(Node *p_node);
	void _watch_assets();
	void _make_picker(Terrain3DAssets::AssetType p_type, const String &p_tooltip, const String &p_add_tooltip);
	void _fill_pickers();
	void _fill_picker(Terrain3DAssets::AssetType p_type);
	void _add_asset(Terrain3DAssets::AssetType p_type);
	void _edit_asset(Terrain3DAssets::AssetType p_type);
	int _picked(Terrain3DAssets::AssetType p_type) const;
	Decal *_decal() const;
	void _show_decal(const Vector3 &p_position);
	void _hide_decal();
	bool _pointer_on_terrain(Camera3D *p_camera, const Ref<InputEvent> &p_event, Vector3 &r_position) const;

public:
	virtual String get_plugin_name() const override { return "Terrain3D"; }
	virtual bool handles(Object *p_object) const override;
	virtual void edit(Object *p_object) override;
	virtual void make_visible(bool p_visible) override;
	virtual EditorPlugin::AfterGUIInput forward_3d_gui_input(Camera3D *p_camera, const Ref<InputEvent> &p_event) override;

	TerrainEditorPlugin();
	~TerrainEditorPlugin();
};
