#include "terrain_editor_plugin.h"

#include "terrain_import_dialog.h"

#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/gui/editor_spin_slider.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/decal.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/color_picker.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/option_button.h"
#include "scene/gui/separator.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/image_texture.h"

#include <iterator>

namespace {
constexpr int BRUSH_PIXELS = 128;
constexpr real_t DECAL_DEPTH = 2000.0; // Tall enough to reach the ground from any height.
} //namespace

const TerrainEditorPlugin::ToolInfo TerrainEditorPlugin::TOOLS[] = {
	{ "Add Region", "Add regions (the squares terrain is made of) where you drag.", Terrain3DEditor::REGION, Terrain3DEditor::ADD },
	{ "Remove Region", "Remove the regions you drag over.", Terrain3DEditor::REGION, Terrain3DEditor::SUBTRACT },
	{ "Raise", "Raise the ground.", Terrain3DEditor::SCULPT, Terrain3DEditor::ADD },
	{ "Lower", "Lower the ground.", Terrain3DEditor::SCULPT, Terrain3DEditor::SUBTRACT },
	{ "Smooth", "Smooth the ground's bumps.", Terrain3DEditor::SCULPT, Terrain3DEditor::AVERAGE },
	{ "Flatten", "Bring the ground to the height set beside the tools.", Terrain3DEditor::HEIGHT, Terrain3DEditor::REPLACE },
	{ "Texture", "Paint the chosen texture.", Terrain3DEditor::TEXTURE, Terrain3DEditor::REPLACE },
	{ "Blend", "Blend the chosen texture into the ground's.", Terrain3DEditor::TEXTURE, Terrain3DEditor::ADD },
	{ "Auto Texture", "Let the ground choose its textures by slope, as the material's auto shader does. Hold Ctrl to stop it.", Terrain3DEditor::AUTOSHADER, Terrain3DEditor::ADD },
	{ "Color", "Tint the ground with a color.", Terrain3DEditor::COLOR, Terrain3DEditor::REPLACE },
	{ "Hole", "Cut holes in the ground, for caves and tunnels. Hold Ctrl to fill them in.", Terrain3DEditor::HOLES, Terrain3DEditor::ADD },
	{ "Place", "Place the chosen mesh, scattered over the ground.", Terrain3DEditor::INSTANCER, Terrain3DEditor::ADD },
	{ "Remove", "Remove the chosen mesh from the ground.", Terrain3DEditor::INSTANCER, Terrain3DEditor::SUBTRACT },
	{ "Navigation", "Mark where characters can walk: a NavigationRegion3D's bake then covers only these parts. Hold Ctrl to unmark.", Terrain3DEditor::NAVIGATION, Terrain3DEditor::ADD },
};

TerrainEditorPlugin::TerrainEditorPlugin() {
	editor = memnew(Terrain3DEditor);
	_make_brush();

	toolbar = memnew(HBoxContainer);
	toolbar->hide();
	toolbar->connect(SceneStringName(theme_changed), callable_mp(this, &TerrainEditorPlugin::_update_icons));
	add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, toolbar);
	toolbar->add_child(memnew(VSeparator));

	MenuButton *menu = memnew(MenuButton);
	menu->set_text(TTR("Terrain"));
	menu->set_flat(true);
	menu->get_popup()->add_item(TTR("Import Heightmap..."), MENU_IMPORT);
	menu->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &TerrainEditorPlugin::_menu_chosen));
	toolbar->add_child(menu);
	import_dialog = memnew(TerrainImportDialog);
	EditorInterface::get_singleton()->get_base_control()->add_child(import_dialog);
	toolbar->add_child(memnew(VSeparator));

	tool_group.instantiate();
	tool_group->set_allow_unpress(true);
	for (int i = 0; i < int(std::size(TOOLS)); i++) {
		Button *button = memnew(Button);
		button->set_text(TTR(TOOLS[i].name));
		// The key that undoes a marking tool is Cmd on a Mac.
		button->set_tooltip_text(TTR(TOOLS[i].tooltip).replace("Ctrl", OS::get_singleton()->has_feature("macos") ? "Cmd" : "Ctrl"));
		button->set_toggle_mode(true);
		button->set_flat(true);
		button->set_button_group(tool_group);
		button->connect(SceneStringName(toggled), callable_mp(this, &TerrainEditorPlugin::_tool_toggled).bind(i));
		toolbar->add_child(button);
		tool_buttons.push_back(button);
	}
	toolbar->add_child(memnew(VSeparator));

	size_slider = memnew(EditorSpinSlider);
	size_slider->set_label(TTR("Size"));
	size_slider->set_suffix("m");
	size_slider->set_min(1);
	size_slider->set_max(200);
	size_slider->set_allow_greater(true);
	size_slider->set_value(20);
	size_slider->set_custom_minimum_size(Size2(110, 0) * EDSCALE);
	size_slider->connect(SceneStringName(value_changed), callable_mp(this, &TerrainEditorPlugin::_update_brush_data).unbind(1));
	toolbar->add_child(size_slider);

	strength_slider = memnew(EditorSpinSlider);
	strength_slider->set_label(TTR("Strength"));
	strength_slider->set_suffix("%");
	strength_slider->set_min(1);
	strength_slider->set_max(100);
	strength_slider->set_value(30);
	strength_slider->set_custom_minimum_size(Size2(110, 0) * EDSCALE);
	strength_slider->connect(SceneStringName(value_changed), callable_mp(this, &TerrainEditorPlugin::_update_brush_data).unbind(1));
	toolbar->add_child(strength_slider);

	// The slopes painting and placing keep to, in degrees: grass on the flat, rock on the steep.
	slope_min_slider = memnew(EditorSpinSlider);
	slope_min_slider->set_label(TTR("Slope"));
	slope_min_slider->set_suffix(U"°");
	slope_min_slider->set_max(90);
	slope_min_slider->set_tooltip_text(TTR("The gentlest slope to paint or place on."));
	slope_min_slider->set_custom_minimum_size(Size2(90, 0) * EDSCALE);
	slope_min_slider->connect(SceneStringName(value_changed), callable_mp(this, &TerrainEditorPlugin::_update_brush_data).unbind(1));
	toolbar->add_child(slope_min_slider);
	slope_max_slider = memnew(EditorSpinSlider);
	slope_max_slider->set_label(TTR("to"));
	slope_max_slider->set_suffix(U"°");
	slope_max_slider->set_max(90);
	slope_max_slider->set_value(90);
	slope_max_slider->set_tooltip_text(TTR("The steepest slope to paint or place on."));
	slope_max_slider->set_custom_minimum_size(Size2(90, 0) * EDSCALE);
	slope_max_slider->connect(SceneStringName(value_changed), callable_mp(this, &TerrainEditorPlugin::_update_brush_data).unbind(1));
	toolbar->add_child(slope_max_slider);

	height_slider = memnew(EditorSpinSlider);
	height_slider->set_label(TTR("Height"));
	height_slider->set_suffix("m");
	height_slider->set_min(-1000);
	height_slider->set_max(1000);
	height_slider->set_allow_greater(true);
	height_slider->set_allow_lesser(true);
	height_slider->set_step(0.1);
	height_slider->set_custom_minimum_size(Size2(110, 0) * EDSCALE);
	height_slider->connect(SceneStringName(value_changed), callable_mp(this, &TerrainEditorPlugin::_update_brush_data).unbind(1));
	toolbar->add_child(height_slider);

	color_picker = memnew(ColorPickerButton);
	color_picker->set_pick_color(Color(0.4, 0.55, 0.3));
	color_picker->set_edit_alpha(false);
	color_picker->set_custom_minimum_size(Size2(40, 0) * EDSCALE);
	color_picker->set_tooltip_text(TTR("The color to paint."));
	color_picker->connect("color_changed", callable_mp(this, &TerrainEditorPlugin::_update_brush_data).unbind(1));
	toolbar->add_child(color_picker);

	_make_picker(Terrain3DAssets::TYPE_TEXTURE, TTR("The texture to paint."), TTR("Add a texture to the terrain."));
	_make_picker(Terrain3DAssets::TYPE_MESH, TTR("The mesh to place."), TTR("Add a mesh to the terrain."));

	_update_settings();
	connect("scene_saved", callable_mp(this, &TerrainEditorPlugin::_scene_saved));
}

void TerrainEditorPlugin::_make_picker(Terrain3DAssets::AssetType p_type, const String &p_tooltip, const String &p_add_tooltip) {
	AssetPicker &picker = pickers[p_type];
	picker.list = memnew(OptionButton);
	picker.list->set_tooltip_text(p_tooltip);
	picker.list->set_flat(true);
	picker.list->add_theme_constant_override("icon_max_width", 24 * EDSCALE);
	picker.list->set_fit_to_longest_item(false); // Its thumbnails' full size would make it very wide.
	picker.list->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	picker.list->set_custom_minimum_size(Size2(140, 0) * EDSCALE);
	picker.list->connect(SceneStringName(item_selected), callable_mp(this, &TerrainEditorPlugin::_update_brush_data).unbind(1));
	toolbar->add_child(picker.list);

	picker.edit = memnew(Button);
	picker.edit->set_flat(true);
	picker.edit->set_tooltip_text(TTR("Edit it in the inspector."));
	picker.edit->connect(SceneStringName(pressed), callable_mp(this, &TerrainEditorPlugin::_edit_asset).bind(p_type));
	toolbar->add_child(picker.edit);

	picker.add = memnew(Button);
	picker.add->set_flat(true);
	picker.add->set_tooltip_text(p_add_tooltip);
	picker.add->connect(SceneStringName(pressed), callable_mp(this, &TerrainEditorPlugin::_add_asset).bind(p_type));
	toolbar->add_child(picker.add);
}

// A terrain saves its data in a folder beside its scene, which a new scene doesn't have until its
// first save: so that save is followed by another, which writes the data.
void TerrainEditorPlugin::_scene_saved(const String &p_path) {
	// Told before the scene has its new path: look once the save is over, on the next frame (the
	// editor won't save from a deferred call).
	SceneTree::get_singleton()->connect(SNAME("process_frame"), callable_mp(this, &TerrainEditorPlugin::_save_again_if_needed).bind(p_path), CONNECT_ONE_SHOT);
}

void TerrainEditorPlugin::_save_again_if_needed(const String &p_path) {
	Node *scene = EditorInterface::get_singleton()->get_edited_scene_root();
	if (scene != nullptr && scene->get_scene_file_path() == p_path && _needs_data_directory(scene)) {
		EditorInterface::get_singleton()->save_scene();
	}
}

bool TerrainEditorPlugin::_needs_data_directory(Node *p_node) {
	const Terrain3D *terrain = Object::cast_to<Terrain3D>(p_node);
	if (terrain != nullptr && terrain->get_data_directory().is_empty() && terrain->get_data() != nullptr && terrain->get_data()->get_region_count() > 0) {
		return true;
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		if (_needs_data_directory(p_node->get_child(i))) {
			return true;
		}
	}
	return false;
}

void TerrainEditorPlugin::_menu_chosen(int p_id) {
	Terrain3D *terrain = _terrain();
	if (terrain != nullptr && p_id == MENU_IMPORT) {
		import_dialog->start(terrain, editor);
	}
}

void TerrainEditorPlugin::_update_icons() {
	for (AssetPicker &picker : pickers) {
		picker.edit->set_button_icon(toolbar->get_editor_theme_icon(SNAME("Edit")));
		picker.add->set_button_icon(toolbar->get_editor_theme_icon(SNAME("Add")));
	}
}

TerrainEditorPlugin::~TerrainEditorPlugin() {
	_hide_decal();
	if (toolbar != nullptr) {
		remove_control_from_container(CONTAINER_SPATIAL_EDITOR_MENU, toolbar);
		memdelete(toolbar);
	}
	memdelete(editor);
}

// A round brush, strongest in the middle and fading to nothing at the edge. Its red channel is
// the strength the tools use; its alpha draws the circle on the ground.
void TerrainEditorPlugin::_make_brush() {
	brush_image = Image::create_empty(BRUSH_PIXELS, BRUSH_PIXELS, false, Image::FORMAT_RGBA8);
	const Vector2 middle = Vector2(BRUSH_PIXELS, BRUSH_PIXELS) * 0.5;
	for (int y = 0; y < BRUSH_PIXELS; y++) {
		for (int x = 0; x < BRUSH_PIXELS; x++) {
			const real_t distance = (Vector2(x + 0.5, y + 0.5) - middle).length() / middle.x;
			const real_t strength = distance >= 1.0 ? 0.0 : Math::smoothstep(real_t(1.0), real_t(0.0), distance);
			const real_t ring = distance < 1.0 && distance > 0.94 ? 1.0 : 0.0;
			brush_image->set_pixel(x, y, Color(strength, strength, strength, MAX(ring, strength * 0.35)));
		}
	}
	brush_texture = ImageTexture::create_from_image(brush_image);
}

Terrain3D *TerrainEditorPlugin::_terrain() const {
	return Object::cast_to<Terrain3D>(ObjectDB::get_instance(terrain_id));
}

bool TerrainEditorPlugin::handles(Object *p_object) const {
	return Object::cast_to<Terrain3D>(p_object) != nullptr;
}

void TerrainEditorPlugin::edit(Object *p_object) {
	Terrain3D *terrain = Object::cast_to<Terrain3D>(p_object);
	if (terrain == nullptr) {
		return;
	}
	_hide_decal();
	terrain_id = terrain->get_instance_id();
	terrain->set_plugin(this);
	terrain->set_editor(editor);
	editor->set_terrain(terrain);
	_watch_assets();
}

// Keeps the pickers in step with the terrain's assets, which can change in the inspector, by
// undo, or when another assets resource is set.
void TerrainEditorPlugin::_watch_assets() {
	Terrain3D *terrain = _terrain();
	const Ref<Terrain3DAssets> assets = terrain != nullptr ? terrain->get_assets() : Ref<Terrain3DAssets>();
	const Callable refill = callable_mp(this, &TerrainEditorPlugin::_fill_pickers);
	if (assets != watched_assets) {
		if (watched_assets.is_valid() && watched_assets->is_connected("textures_changed", refill)) {
			watched_assets->disconnect("textures_changed", refill);
			watched_assets->disconnect("meshes_changed", refill);
		}
		watched_assets = assets;
		if (watched_assets.is_valid()) {
			watched_assets->connect("textures_changed", refill);
			watched_assets->connect("meshes_changed", refill);
		}
	}
	_fill_pickers();
}

void TerrainEditorPlugin::_fill_pickers() {
	_fill_picker(Terrain3DAssets::TYPE_TEXTURE);
	_fill_picker(Terrain3DAssets::TYPE_MESH);
	_update_brush_data();
}

void TerrainEditorPlugin::_fill_picker(Terrain3DAssets::AssetType p_type) {
	OptionButton *list = pickers[p_type].list;
	const int selected = list->get_selected_id();
	list->clear();
	if (watched_assets.is_valid()) {
		const int count = p_type == Terrain3DAssets::TYPE_TEXTURE ? watched_assets->get_texture_count() : watched_assets->get_mesh_count();
		for (int i = 0; i < count; i++) {
			Ref<Terrain3DAssetResource> asset;
			if (p_type == Terrain3DAssets::TYPE_TEXTURE) {
				asset = watched_assets->get_texture_asset(i);
			} else {
				asset = watched_assets->get_mesh_asset(i);
			}
			if (asset.is_valid()) {
				list->add_icon_item(asset->get_thumbnail(), asset->get_asset_name(), i);
			}
		}
	}
	const bool any = list->get_item_count() > 0;
	if (!any) {
		list->add_item(p_type == Terrain3DAssets::TYPE_TEXTURE ? TTR("No textures") : TTR("No meshes"), -1);
	}
	list->set_disabled(!any);
	pickers[p_type].edit->set_disabled(!any);
	const int index = list->get_item_index(selected);
	list->select(index >= 0 ? index : 0);
}

int TerrainEditorPlugin::_picked(Terrain3DAssets::AssetType p_type) const {
	return pickers[p_type].list->get_selected_id();
}

// A new asset at the next free ID, open in the inspector to choose its images or mesh.
void TerrainEditorPlugin::_add_asset(Terrain3DAssets::AssetType p_type) {
	if (watched_assets.is_null()) {
		return;
	}
	const bool textures = p_type == Terrain3DAssets::TYPE_TEXTURE;
	const int id = textures ? watched_assets->get_texture_count() : watched_assets->get_mesh_count();
	const int max = textures ? Terrain3DAssets::MAX_TEXTURES : Terrain3DAssets::MAX_MESHES;
	if (id >= max) {
		EditorNode::get_singleton()->show_warning(vformat(textures ? TTR("A terrain can have up to %d textures.") : TTR("A terrain can have up to %d meshes."), max));
		return;
	}
	Ref<Terrain3DAssetResource> asset;
	if (textures) {
		asset = Ref<Terrain3DTextureAsset>(memnew(Terrain3DTextureAsset));
	} else {
		asset = Ref<Terrain3DMeshAsset>(memnew(Terrain3DMeshAsset));
	}
	asset->set_id(id);
	const Array before = (textures ? Array(watched_assets->get_texture_list()) : Array(watched_assets->get_mesh_list())).duplicate();
	Array after = before.duplicate();
	after.push_back(asset);
	const StringName setter = textures ? "set_texture_list" : "set_mesh_list";
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(textures ? TTR("Add Terrain Texture") : TTR("Add Terrain Mesh"), UndoRedo::MERGE_DISABLE, _terrain());
	undo_redo->add_do_method(watched_assets.ptr(), setter, after);
	undo_redo->add_undo_method(watched_assets.ptr(), setter, before);
	undo_redo->commit_action();
	pickers[p_type].list->select(pickers[p_type].list->get_item_index(id));
	_update_brush_data();
	_edit_asset(p_type);
}

void TerrainEditorPlugin::_edit_asset(Terrain3DAssets::AssetType p_type) {
	const int id = _picked(p_type);
	if (watched_assets.is_null() || id < 0) {
		return;
	}
	Object *asset = p_type == Terrain3DAssets::TYPE_TEXTURE ? (Object *)watched_assets->get_texture_asset(id).ptr() : (Object *)watched_assets->get_mesh_asset(id).ptr();
	// Only the inspector changes, so the terrain stays selected and its tools stay open.
	EditorInterface::get_singleton()->inspect_object(asset, "", true);
}

void TerrainEditorPlugin::make_visible(bool p_visible) {
	toolbar->set_visible(p_visible);
	if (!p_visible) {
		if (current_tool >= 0) {
			tool_buttons[current_tool]->set_pressed(false);
		}
		_hide_decal();
	}
}

void TerrainEditorPlugin::_tool_toggled(bool p_pressed, int p_tool) {
	current_tool = p_pressed ? p_tool : (current_tool == p_tool ? -1 : current_tool);
	if (current_tool < 0) {
		_hide_decal();
	}
	_update_settings();
}

// Only the settings the chosen tool uses.
void TerrainEditorPlugin::_update_settings() {
	const bool regions = current_tool >= 0 && TOOLS[current_tool].tool == Terrain3DEditor::REGION;
	const Terrain3DEditor::Tool tool = current_tool >= 0 ? TOOLS[current_tool].tool : Terrain3DEditor::TOOL_MAX;
	// Marking tools set something or don't: there's no strength to it.
	const bool marking = tool == Terrain3DEditor::HOLES || tool == Terrain3DEditor::NAVIGATION || tool == Terrain3DEditor::AUTOSHADER;
	const bool sloped = tool == Terrain3DEditor::TEXTURE || (tool == Terrain3DEditor::INSTANCER && TOOLS[current_tool].operation == Terrain3DEditor::ADD);
	size_slider->set_visible(current_tool >= 0 && !regions);
	strength_slider->set_visible(current_tool >= 0 && !regions && !marking);
	slope_min_slider->set_visible(sloped);
	slope_max_slider->set_visible(sloped);
	height_slider->set_visible(current_tool >= 0 && TOOLS[current_tool].tool == Terrain3DEditor::HEIGHT);
	color_picker->set_visible(current_tool >= 0 && TOOLS[current_tool].tool == Terrain3DEditor::COLOR);
	for (int type = 0; type < 2; type++) {
		const Terrain3DEditor::Tool uses = type == Terrain3DAssets::TYPE_TEXTURE ? Terrain3DEditor::TEXTURE : Terrain3DEditor::INSTANCER;
		const bool shown = current_tool >= 0 && TOOLS[current_tool].tool == uses;
		pickers[type].list->set_visible(shown);
		pickers[type].edit->set_visible(shown);
		pickers[type].add->set_visible(shown);
	}
	_update_brush_data();
}

void TerrainEditorPlugin::_update_brush_data() {
	brush_data["brush"] = Array({ brush_image, brush_texture });
	brush_data["size"] = size_slider->get_value();
	brush_data["strength"] = strength_slider->get_value();
	brush_data["height"] = height_slider->get_value();
	brush_data["color"] = color_picker->get_pick_color();
	brush_data["roughness"] = 0.0;
	brush_data["slope"] = Vector2(slope_min_slider->get_value(), slope_max_slider->get_value());
	brush_data["texture_asset_ids"] = TypedArray<int>({ MAX(_picked(Terrain3DAssets::TYPE_TEXTURE), 0) });
	brush_data["mesh_asset_ids"] = TypedArray<int>({ MAX(_picked(Terrain3DAssets::TYPE_MESH), 0) });
	brush_data["enable_texture"] = true;
	brush_data["enable_angle"] = false; // Painting keeps the texture's rotation and scale.
	brush_data["enable_scale"] = false;
	brush_data["mouse_pressure"] = 1.0;
	brush_data["auto_regions"] = true;
	brush_data["align_to_view"] = false;
	brush_data["brush_spin_speed"] = 0.0;
	brush_data["gamma"] = 1.0;
	brush_data["modifier_shift"] = false;
	brush_data["modifier_ctrl"] = false;
	brush_data["modifier_alt"] = false;
	if (Decal *brush_decal = _decal()) {
		const real_t size = size_slider->get_value();
		brush_decal->set_size(Vector3(size, DECAL_DEPTH, size));
	}
}

// Held by ID: it goes with the terrain it's under, which can be freed (its scene closed) first.
Decal *TerrainEditorPlugin::_decal() const {
	return Object::cast_to<Decal>(ObjectDB::get_instance(decal_id));
}

void TerrainEditorPlugin::_show_decal(const Vector3 &p_position) {
	Terrain3D *terrain = _terrain();
	if (terrain == nullptr) {
		return;
	}
	Decal *brush_decal = _decal();
	if (brush_decal == nullptr) {
		brush_decal = memnew(Decal);
		decal_id = brush_decal->get_instance_id();
		brush_decal->set_texture(Decal::TEXTURE_ALBEDO, brush_texture);
		brush_decal->set_modulate(Color(1.0, 0.85, 0.4));
		brush_decal->set_upper_fade(0.0);
		brush_decal->set_lower_fade(0.0);
		// Not saved with the scene: a child the editor adds while a tool is in use.
		terrain->add_child(brush_decal, false, Node::INTERNAL_MODE_BACK);
		_update_brush_data();
	}
	const bool regions = current_tool >= 0 && TOOLS[current_tool].tool == Terrain3DEditor::REGION;
	if (regions) {
		// A region's square, under the pointer.
		const real_t side = real_t(terrain->get_region_size()) * terrain->get_vertex_spacing();
		const Vector3 corner = Vector3(Math::floor(p_position.x / side) * side, 0, Math::floor(p_position.z / side) * side);
		brush_decal->set_size(Vector3(side, DECAL_DEPTH, side));
		brush_decal->set_global_position(corner + Vector3(side * 0.5, p_position.y, side * 0.5));
	} else {
		const real_t size = size_slider->get_value();
		brush_decal->set_size(Vector3(size, DECAL_DEPTH, size));
		brush_decal->set_global_position(p_position);
	}
	brush_decal->show();
}

void TerrainEditorPlugin::_hide_decal() {
	if (Decal *brush_decal = _decal()) {
		brush_decal->queue_free();
	}
	decal_id = ObjectID();
}

// Where the pointer meets the ground: or, beyond the terrain, the flat plane at height 0 (so
// regions can be added where there's no terrain yet).
bool TerrainEditorPlugin::_pointer_on_terrain(Camera3D *p_camera, const Ref<InputEvent> &p_event, Vector3 &r_position) const {
	Terrain3D *terrain = _terrain();
	const Ref<InputEventMouse> mouse = p_event;
	if (terrain == nullptr || mouse.is_null()) {
		return false;
	}
	const Vector2 at = mouse->get_position();
	const Vector3 from = p_camera->project_ray_origin(at);
	const Vector3 direction = p_camera->project_ray_normal(at);
	terrain->set_camera(p_camera);
	const Vector3 hit = terrain->get_intersection(from, direction, true);
	if (!Math::is_nan(hit.y) && hit.z < 3.4e38) {
		r_position = hit;
		return true;
	}
	if (direction.y >= -0.0001) {
		return false;
	}
	r_position = from + direction * (-from.y / direction.y);
	return true;
}

EditorPlugin::AfterGUIInput TerrainEditorPlugin::forward_3d_gui_input(Camera3D *p_camera, const Ref<InputEvent> &p_event) {
	if (current_tool < 0 || _terrain() == nullptr) {
		return AFTER_GUI_INPUT_PASS;
	}
	const Ref<InputEventMouseButton> button = p_event;
	const Ref<InputEventMouseMotion> motion = p_event;
	Vector3 position;
	const bool on_ground = _pointer_on_terrain(p_camera, p_event, position);

	if (motion.is_valid()) {
		if (on_ground) {
			_show_decal(position);
		}
		if (painting && on_ground) {
			editor->operate(position, p_camera->get_global_rotation().y);
			return AFTER_GUI_INPUT_STOP;
		}
		return AFTER_GUI_INPUT_PASS;
	}

	if (button.is_valid() && button->get_button_index() == MouseButton::LEFT) {
		if (button->is_pressed() && on_ground) {
			const ToolInfo &tool = TOOLS[current_tool];
			if ((tool.tool == Terrain3DEditor::TEXTURE && _picked(Terrain3DAssets::TYPE_TEXTURE) < 0) ||
					(tool.tool == Terrain3DEditor::INSTANCER && _picked(Terrain3DAssets::TYPE_MESH) < 0)) {
				return AFTER_GUI_INPUT_STOP; // Nothing to paint or place yet.
			}
			_update_brush_data();
			editor->set_tool(tool.tool);
			// Ctrl undoes what a marking tool does: fills holes, unmarks navigation, stops auto texturing.
			const bool marking = tool.tool == Terrain3DEditor::HOLES || tool.tool == Terrain3DEditor::NAVIGATION || tool.tool == Terrain3DEditor::AUTOSHADER;
			editor->set_operation(marking && button->is_command_or_control_pressed() ? Terrain3DEditor::SUBTRACT : tool.operation);
			editor->set_brush_data(brush_data.duplicate()); // A copy: it rescales the values in place.
			editor->start_operation(position);
			editor->operate(position, p_camera->get_global_rotation().y);
			painting = true;
			return AFTER_GUI_INPUT_STOP;
		}
		if (!button->is_pressed() && painting) {
			painting = false;
			editor->stop_operation();
			return AFTER_GUI_INPUT_STOP;
		}
	}
	return AFTER_GUI_INPUT_PASS;
}
