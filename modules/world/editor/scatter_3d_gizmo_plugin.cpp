#include "scatter_3d_gizmo_plugin.h"

#include "../scatter_3d.h"

#include "editor/settings/editor_settings.h"

Scatter3DGizmoPlugin::Scatter3DGizmoPlugin() {
	helper.instantiate();
	const Color colour = EDITOR_GET("editors/3d_gizmos/gizmo_colors/visibility_notifier");
	create_material("scatter_material", colour);
	create_handle_material("handles");
}

bool Scatter3DGizmoPlugin::has_gizmo(Node3D *p_node) {
	return Object::cast_to<Scatter3D>(p_node) != nullptr;
}

String Scatter3DGizmoPlugin::get_gizmo_name() const {
	return "Scatter3D";
}

int Scatter3DGizmoPlugin::get_priority() const {
	return -1;
}

String Scatter3DGizmoPlugin::get_handle_name(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary) const {
	return helper->box_get_handle_name(p_id);
}

Variant Scatter3DGizmoPlugin::get_handle_value(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary) const {
	return Object::cast_to<Scatter3D>(p_gizmo->get_node_3d())->get_size();
}

void Scatter3DGizmoPlugin::begin_handle_action(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary) {
	helper->initialize_handle_action(get_handle_value(p_gizmo, p_id, p_secondary), p_gizmo->get_node_3d()->get_global_transform());
}

void Scatter3DGizmoPlugin::set_handle(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary, Camera3D *p_camera, const Point2 &p_point) {
	Scatter3D *scatter = Object::cast_to<Scatter3D>(p_gizmo->get_node_3d());
	Vector3 segment[2];
	helper->get_segment(p_camera, p_point, segment);
	Vector3 size = scatter->get_size();
	Vector3 position;
	helper->box_set_handle(segment, p_id, size, position);
	scatter->set_size(size);
	scatter->set_global_position(position);
}

void Scatter3DGizmoPlugin::commit_handle(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary, const Variant &p_restore, bool p_cancel) {
	helper->box_commit_handle(TTR("Change Scatter Size"), p_cancel, p_gizmo->get_node_3d());
}

void Scatter3DGizmoPlugin::redraw(EditorNode3DGizmo *p_gizmo) {
	p_gizmo->clear();
	Scatter3D *scatter = Object::cast_to<Scatter3D>(p_gizmo->get_node_3d());
	const Vector3 size = scatter->get_size();
	const AABB box(-size / 2, size);
	Vector<Vector3> lines;
	for (int i = 0; i < 12; i++) {
		Vector3 a;
		Vector3 b;
		box.get_edge(i, a, b);
		lines.push_back(a);
		lines.push_back(b);
	}
	p_gizmo->add_lines(lines, get_material("scatter_material", p_gizmo));
	p_gizmo->add_collision_segments(lines);
	if (p_gizmo->is_selected()) {
		p_gizmo->add_handles(helper->box_get_handles(size), get_material("handles"));
	}
}
