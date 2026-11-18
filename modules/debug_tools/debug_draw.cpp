#include "debug_draw.h"

#include "../world/world_origin.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/font.h"
#include "scene/resources/material.h"
#include "scene/resources/world_2d.h"
#include "scene/theme/theme_db.h"
#include "servers/rendering/rendering_server.h"

DebugDraw *DebugDraw::singleton = nullptr;

namespace {
constexpr int CIRCLE_SEGMENTS = 32;
constexpr int OVERLAY_LAYER = (1 << 20) - 1; // Above any CanvasLayer a game would use (the console goes over it).
constexpr float SEE_THROUGH_ALPHA = 0.25;

uint64_t now_usec() {
	return OS::get_singleton()->get_ticks_usec();
}
} // namespace

void DebugDraw::Shapes::clear() {
	lines.clear();
	line_colors.clear();
	lines_2d.clear();
	line_colors_2d.clear();
	texts.clear();
	texts_2d.clear();
}

void DebugDraw::Shapes::append(const Shapes &p_other) {
	lines.append_array(p_other.lines);
	line_colors.append_array(p_other.line_colors);
	lines_2d.append_array(p_other.lines_2d);
	line_colors_2d.append_array(p_other.line_colors_2d);
	texts.append_array(p_other.texts);
	texts_2d.append_array(p_other.texts_2d);
}

void DebugDraw::Shapes::move(const Vector3 &p_by) {
	Vector3 *points = lines.ptrw();
	for (int i = 0; i < lines.size(); i++) {
		points[i] += p_by;
	}
	Text3D *labels = texts.ptrw();
	for (int i = 0; i < texts.size(); i++) {
		labels[i].position += p_by;
	}
}

DebugDraw::DebugDraw() {
	singleton = this;
	enabled = GLOBAL_DEF("debug_draw/enabled_in_release_builds", false);
#ifdef DEBUG_ENABLED
	enabled = true;
#endif
	if (!Engine::get_singleton()->is_editor_hint()) {
		callable_mp(this, &DebugDraw::_start).call_deferred();
	}
}

DebugDraw::~DebugDraw() {
	_free_resources();
	singleton = nullptr;
}

void DebugDraw::_start() {
	SceneTree *tree = SceneTree::get_singleton();
	if (started || tree == nullptr) {
		return;
	}
	started = true;
	tree->connect(SNAME("process_frame"), callable_mp(this, &DebugDraw::_process_frame));
	tree->connect(SNAME("physics_frame"), callable_mp(this, &DebugDraw::_physics_frame));
	RS::get_singleton()->connect(SNAME("frame_pre_draw"), callable_mp(this, &DebugDraw::_pre_draw));
	if (WorldOrigin::get_singleton() != nullptr) {
		WorldOrigin::get_singleton()->connect(SNAME("shifted"), callable_mp(this, &DebugDraw::_shifted));
	}
}

void DebugDraw::_free_resources() {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs == nullptr) {
		return;
	}
	for (RID *rid : { &instance, &see_through_instance, &mesh, &world_item, &overlay_item, &overlay }) {
		if (rid->is_valid()) {
			rs->free_rid(*rid);
			*rid = RID();
		}
	}
	material.unref();
	see_through_material.unref();
	performance.stop(false);
}

void DebugDraw::set_enabled(bool p_enabled) {
	MutexLock lock(mutex);
	if (enabled == p_enabled) {
		return;
	}
	enabled = p_enabled;
	if (!enabled) {
		clear();
	}
}

bool DebugDraw::is_enabled() const {
	return enabled;
}

void DebugDraw::set_performance_overlay(bool p_shown) {
	MutexLock lock(mutex);
	performance_overlay = p_shown;
	if (!performance_overlay) {
		performance.stop(); // Measuring the GPU costs a little: only while it's shown.
	}
}

bool DebugDraw::is_performance_overlay() const {
	return performance_overlay;
}

// Shapes drawn for a while go in their own batch; the rest last until the frame is drawn, or
// until the next physics tick when drawn during one.
DebugDraw::Shapes &DebugDraw::_target(float p_duration) {
	if (p_duration > 0.0) {
		Timed timed_shapes;
		timed_shapes.until_usec = now_usec() + uint64_t(p_duration * 1000000.0);
		timed.push_back(timed_shapes);
		return timed.write[timed.size() - 1].shapes;
	}
	return Engine::get_singleton()->is_in_physics_frame() ? tick_shapes : frame_shapes;
}

void DebugDraw::_add_line(Shapes &p_shapes, const Vector3 &p_from, const Vector3 &p_to, const Color &p_color) {
	p_shapes.lines.push_back(p_from);
	p_shapes.lines.push_back(p_to);
	p_shapes.line_colors.push_back(p_color);
	p_shapes.line_colors.push_back(p_color);
}

void DebugDraw::_add_line_2d(Shapes &p_shapes, const Vector2 &p_from, const Vector2 &p_to, const Color &p_color) {
	p_shapes.lines_2d.push_back(p_from);
	p_shapes.lines_2d.push_back(p_to);
	p_shapes.line_colors_2d.push_back(p_color);
}

void DebugDraw::_add_circle(Shapes &p_shapes, const Vector3 &p_center, const Vector3 &p_axis_a, const Vector3 &p_axis_b, const Color &p_color) {
	Vector3 previous = p_center + p_axis_a;
	for (int i = 1; i <= CIRCLE_SEGMENTS; i++) {
		const float angle = Math::TAU * i / CIRCLE_SEGMENTS;
		const Vector3 next = p_center + p_axis_a * Math::cos(angle) + p_axis_b * Math::sin(angle);
		_add_line(p_shapes, previous, next, p_color);
		previous = next;
	}
}

void DebugDraw::line(const Vector3 &p_from, const Vector3 &p_to, const Color &p_color, float p_duration) {
	MutexLock lock(mutex);
	if (_accepting()) {
		_add_line(_target(p_duration), p_from, p_to, p_color);
	}
}

void DebugDraw::path(const PackedVector3Array &p_points, const Color &p_color, float p_duration, bool p_closed) {
	MutexLock lock(mutex);
	if (!_accepting() || p_points.size() < 2) {
		return;
	}
	Shapes &shapes = _target(p_duration);
	for (int i = 1; i < p_points.size(); i++) {
		_add_line(shapes, p_points[i - 1], p_points[i], p_color);
	}
	if (p_closed && p_points.size() > 2) {
		_add_line(shapes, p_points[p_points.size() - 1], p_points[0], p_color);
	}
}

void DebugDraw::arrow(const Vector3 &p_from, const Vector3 &p_to, const Color &p_color, float p_duration) {
	MutexLock lock(mutex);
	if (!_accepting()) {
		return;
	}
	Shapes &shapes = _target(p_duration);
	_add_line(shapes, p_from, p_to, p_color);
	const Vector3 along = p_to - p_from;
	const float length = along.length();
	if (length < CMP_EPSILON) {
		return;
	}
	// A head of four strokes, a fifth of the arrow long (at most half a metre).
	const Vector3 direction = along / length;
	const Vector3 side = direction.get_any_perpendicular();
	const Vector3 up = direction.cross(side);
	const float head = MIN(length * 0.2, 0.5);
	const Vector3 base = p_to - direction * head;
	for (const Vector3 &offset : { side, -side, up, -up }) {
		_add_line(shapes, p_to, base + offset * head * 0.4, p_color);
	}
}

void DebugDraw::point(const Vector3 &p_position, float p_size, const Color &p_color, float p_duration) {
	MutexLock lock(mutex);
	if (!_accepting()) {
		return;
	}
	Shapes &shapes = _target(p_duration);
	const float half = p_size * 0.5;
	_add_line(shapes, p_position - Vector3(half, 0, 0), p_position + Vector3(half, 0, 0), p_color);
	_add_line(shapes, p_position - Vector3(0, half, 0), p_position + Vector3(0, half, 0), p_color);
	_add_line(shapes, p_position - Vector3(0, 0, half), p_position + Vector3(0, 0, half), p_color);
}

void DebugDraw::box(const Transform3D &p_transform, const Color &p_color, float p_duration) {
	MutexLock lock(mutex);
	if (!_accepting()) {
		return;
	}
	Shapes &shapes = _target(p_duration);
	Vector3 corners[8];
	for (int i = 0; i < 8; i++) {
		corners[i] = p_transform.xform(Vector3(i & 1 ? 0.5 : -0.5, i & 2 ? 0.5 : -0.5, i & 4 ? 0.5 : -0.5));
	}
	// Each edge joins two corners that differ in one axis.
	for (int i = 0; i < 8; i++) {
		for (int bit : { 1, 2, 4 }) {
			if (!(i & bit)) {
				_add_line(shapes, corners[i], corners[i | bit], p_color);
			}
		}
	}
}

void DebugDraw::aabb(const AABB &p_aabb, const Color &p_color, float p_duration) {
	box(Transform3D(Basis::from_scale(p_aabb.size), p_aabb.get_center()), p_color, p_duration);
}

void DebugDraw::sphere(const Vector3 &p_center, float p_radius, const Color &p_color, float p_duration) {
	MutexLock lock(mutex);
	if (!_accepting()) {
		return;
	}
	Shapes &shapes = _target(p_duration);
	_add_circle(shapes, p_center, Vector3(p_radius, 0, 0), Vector3(0, p_radius, 0), p_color);
	_add_circle(shapes, p_center, Vector3(p_radius, 0, 0), Vector3(0, 0, p_radius), p_color);
	_add_circle(shapes, p_center, Vector3(0, p_radius, 0), Vector3(0, 0, p_radius), p_color);
}

void DebugDraw::axes(const Transform3D &p_transform, float p_size, float p_duration) {
	const Vector3 origin = p_transform.origin;
	arrow(origin, origin + p_transform.basis.get_column(0).normalized() * p_size, Color(0.96, 0.2, 0.32), p_duration);
	arrow(origin, origin + p_transform.basis.get_column(1).normalized() * p_size, Color(0.53, 0.84, 0.01), p_duration);
	arrow(origin, origin + p_transform.basis.get_column(2).normalized() * p_size, Color(0.16, 0.55, 0.96), p_duration);
}

void DebugDraw::text(const Vector3 &p_position, const String &p_text, const Color &p_color, float p_duration) {
	MutexLock lock(mutex);
	if (_accepting()) {
		_target(p_duration).texts.push_back({ p_position, p_text, p_color });
	}
}

void DebugDraw::line_2d(const Vector2 &p_from, const Vector2 &p_to, const Color &p_color, float p_duration) {
	MutexLock lock(mutex);
	if (_accepting()) {
		_add_line_2d(_target(p_duration), p_from, p_to, p_color);
	}
}

void DebugDraw::path_2d(const PackedVector2Array &p_points, const Color &p_color, float p_duration, bool p_closed) {
	MutexLock lock(mutex);
	if (!_accepting() || p_points.size() < 2) {
		return;
	}
	Shapes &shapes = _target(p_duration);
	for (int i = 1; i < p_points.size(); i++) {
		_add_line_2d(shapes, p_points[i - 1], p_points[i], p_color);
	}
	if (p_closed && p_points.size() > 2) {
		_add_line_2d(shapes, p_points[p_points.size() - 1], p_points[0], p_color);
	}
}

void DebugDraw::arrow_2d(const Vector2 &p_from, const Vector2 &p_to, const Color &p_color, float p_duration) {
	MutexLock lock(mutex);
	if (!_accepting()) {
		return;
	}
	Shapes &shapes = _target(p_duration);
	_add_line_2d(shapes, p_from, p_to, p_color);
	const Vector2 along = p_to - p_from;
	const float length = along.length();
	if (length < CMP_EPSILON) {
		return;
	}
	const Vector2 direction = along / length;
	const float head = MIN(length * 0.2, 16.0);
	const Vector2 base = p_to - direction * head;
	const Vector2 side = direction.orthogonal() * head * 0.5;
	_add_line_2d(shapes, p_to, base + side, p_color);
	_add_line_2d(shapes, p_to, base - side, p_color);
}

void DebugDraw::rect_2d(const Rect2 &p_rect, const Color &p_color, float p_duration) {
	MutexLock lock(mutex);
	const Vector2 a = p_rect.position;
	const Vector2 b = p_rect.position + p_rect.size;
	PackedVector2Array corners = { a, Vector2(b.x, a.y), b, Vector2(a.x, b.y) };
	path_2d(corners, p_color, p_duration, true);
}

void DebugDraw::circle_2d(const Vector2 &p_center, float p_radius, const Color &p_color, float p_duration) {
	MutexLock lock(mutex);
	PackedVector2Array points;
	points.resize(CIRCLE_SEGMENTS);
	for (int i = 0; i < CIRCLE_SEGMENTS; i++) {
		points.write[i] = p_center + Vector2(p_radius, 0).rotated(Math::TAU * i / CIRCLE_SEGMENTS);
	}
	path_2d(points, p_color, p_duration, true);
}

void DebugDraw::text_2d(const Vector2 &p_position, const String &p_text, const Color &p_color, float p_duration) {
	MutexLock lock(mutex);
	if (_accepting()) {
		_target(p_duration).texts_2d.push_back({ p_position, p_text, p_color });
	}
}

void DebugDraw::show_value(const String &p_name, const Variant &p_value, float p_duration) {
	MutexLock lock(mutex);
	if (!_accepting()) {
		return;
	}
	Value value;
	if (p_value.get_type() == Variant::FLOAT) {
		value.text = String::num(double(p_value), 3);
	} else {
		value.text = p_value.stringify();
	}
	value.until_usec = p_duration > 0.0 ? now_usec() + uint64_t(p_duration * 1000000.0) : 0;
	value.from_physics = Engine::get_singleton()->is_in_physics_frame();
	values[p_name] = value;
}

void DebugDraw::clear() {
	MutexLock lock(mutex);
	frame_shapes.clear();
	tick_shapes.clear();
	timed.clear();
	values.clear();
	if (started) {
		RenderingServer *rs = RenderingServer::get_singleton();
		if (mesh.is_valid()) {
			rs->mesh_clear(mesh);
		}
		for (const RID &item : { world_item, overlay_item }) {
			if (item.is_valid()) {
				rs->canvas_item_clear(item);
			}
		}
	}
}

int DebugDraw::get_line_count() const {
	MutexLock lock(mutex);
	int count = (frame_shapes.lines.size() + tick_shapes.lines.size() + frame_shapes.lines_2d.size() + tick_shapes.lines_2d.size()) / 2;
	for (const Timed &batch : timed) {
		count += (batch.shapes.lines.size() + batch.shapes.lines_2d.size()) / 2;
	}
	return count;
}

void DebugDraw::_expire(uint64_t p_now) {
	for (int i = timed.size() - 1; i >= 0; i--) {
		if (timed[i].until_usec <= p_now) {
			timed.remove_at(i);
		}
	}
	Vector<String> gone;
	for (const KeyValue<String, Value> &entry : values) {
		if (entry.value.until_usec != 0 && entry.value.until_usec <= p_now) {
			gone.push_back(entry.key);
		}
	}
	for (const String &name : gone) {
		values.erase(name);
	}
}

// A frame's shapes go once it's drawn; when nothing draws (headless, or a hidden window), they
// go anyway as the next frame starts.
void DebugDraw::_process_frame() {
	MutexLock lock(mutex);
	if (!drawn_this_frame) {
		frame_shapes.clear();
		_drop_values(false);
		_expire(now_usec());
	}
	drawn_this_frame = false;
}

void DebugDraw::_physics_frame() {
	MutexLock lock(mutex);
	tick_shapes.clear();
	_drop_values(true);
}

// Values shown for no time in particular go with the frame (or tick) they were shown in.
void DebugDraw::_drop_values(bool p_from_physics) {
	Vector<String> gone;
	for (const KeyValue<String, Value> &entry : values) {
		if (entry.value.until_usec == 0 && entry.value.from_physics == p_from_physics) {
			gone.push_back(entry.key);
		}
	}
	for (const String &name : gone) {
		values.erase(name);
	}
}

void DebugDraw::_pre_draw() {
	MutexLock lock(mutex);
	drawn_this_frame = true;
	_expire(now_usec());
	SceneTree *tree = SceneTree::get_singleton();
	Viewport *root = tree != nullptr ? tree->get_root() : nullptr;
	if (root == nullptr) {
		return;
	}
	Shapes all = frame_shapes;
	all.append(tick_shapes);
	for (const Timed &batch : timed) {
		all.append(batch.shapes);
	}
	if (performance_overlay && enabled) {
		_ensure_overlay(root);
		performance.update(root, overlay);
	}
	// Nothing to draw, and nothing drawn last frame to take away: nothing to do.
	const bool has_content = !all.is_empty() || !values.is_empty();
	if (!has_content && !drew_last_frame) {
		return;
	}
	drew_last_frame = has_content;
	_draw_3d(root, all);
	_draw_2d(root, all);
	_draw_overlay(root, all);

	frame_shapes.clear();
	_drop_values(false);
}

void DebugDraw::_draw_3d(Viewport *p_root, const Shapes &p_shapes) {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (p_shapes.lines.is_empty()) {
		if (mesh.is_valid()) {
			rs->mesh_clear(mesh);
		}
		return;
	}
	if (!mesh.is_valid()) {
		mesh = rs->mesh_create();
		material.instantiate();
		material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		material->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG, true);
		material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		// A faint copy over everything, so a line behind a wall still shows where it is.
		see_through_material = material->duplicate();
		see_through_material->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, true);
		see_through_material->set_albedo(Color(1, 1, 1, SEE_THROUGH_ALPHA));
		see_through_material->set_render_priority(Material::RENDER_PRIORITY_MAX);
		instance = rs->instance_create();
		see_through_instance = rs->instance_create();
		for (const RID &each : { instance, see_through_instance }) {
			rs->instance_set_base(each, mesh);
			rs->instance_geometry_set_cast_shadows_setting(each, RSE::SHADOW_CASTING_SETTING_OFF);
		}
		rs->instance_geometry_set_material_override(instance, material->get_rid());
		rs->instance_geometry_set_material_override(see_through_instance, see_through_material->get_rid());
	}
	const Ref<World3D> world = p_root->find_world_3d();
	const RID world_scenario = world.is_valid() ? world->get_scenario() : RID();
	if (world_scenario != scenario) {
		scenario = world_scenario;
		rs->instance_set_scenario(instance, scenario);
		rs->instance_set_scenario(see_through_instance, scenario);
	}

	Array arrays;
	arrays.resize(RSE::ARRAY_MAX);
	arrays[RSE::ARRAY_VERTEX] = PackedVector3Array(p_shapes.lines);
	arrays[RSE::ARRAY_COLOR] = PackedColorArray(p_shapes.line_colors);
	rs->mesh_clear(mesh);
	rs->mesh_add_surface_from_arrays(mesh, RSE::PRIMITIVE_LINES, arrays);
	AABB bounds(p_shapes.lines[0], Vector3());
	for (const Vector3 &each : p_shapes.lines) {
		bounds.expand_to(each);
	}
	const AABB grown = bounds.grow(0.01);
	rs->instance_set_custom_aabb(instance, grown);
	rs->instance_set_custom_aabb(see_through_instance, grown);
}

void DebugDraw::_draw_2d(Viewport *p_root, const Shapes &p_shapes) {
	RenderingServer *rs = RenderingServer::get_singleton();
	const Ref<World2D> world = p_root->find_world_2d();
	const RID canvas = world.is_valid() ? world->get_canvas() : RID();
	if (!world_item.is_valid()) {
		world_item = rs->canvas_item_create();
		rs->canvas_item_set_z_index(world_item, RSE::CANVAS_ITEM_Z_MAX);
	}
	if (canvas != world_canvas) {
		world_canvas = canvas;
		rs->canvas_item_set_parent(world_item, world_canvas);
	}
	rs->canvas_item_clear(world_item);
	if (!p_shapes.lines_2d.is_empty()) {
		rs->canvas_item_add_multiline(world_item, p_shapes.lines_2d, p_shapes.line_colors_2d);
	}
	const Ref<Font> font = ThemeDB::get_singleton()->get_fallback_font();
	const int size = ThemeDB::get_singleton()->get_fallback_font_size();
	for (const Text2D &label : p_shapes.texts_2d) {
		font->draw_string_outline(world_item, label.position, label.text, HORIZONTAL_ALIGNMENT_LEFT, -1, size, 4, Color(0, 0, 0, 0.75 * label.color.a));
		font->draw_string(world_item, label.position, label.text, HORIZONTAL_ALIGNMENT_LEFT, -1, size, label.color);
	}
}

// 3D labels and the list of values, on a canvas above the rest of the screen.
// A canvas above everything else on screen, for the overlay's text and the performance panel.
void DebugDraw::_ensure_overlay(Viewport *p_root) {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!overlay.is_valid()) {
		overlay = rs->canvas_create();
		overlay_item = rs->canvas_item_create();
		rs->canvas_item_set_parent(overlay_item, overlay);
	}
	const RID viewport = p_root->get_viewport_rid();
	if (viewport != overlay_viewport) {
		overlay_viewport = viewport;
		rs->viewport_attach_canvas(viewport, overlay);
		rs->viewport_set_canvas_stacking(viewport, overlay, OVERLAY_LAYER, 0);
	}
}

void DebugDraw::_draw_overlay(Viewport *p_root, const Shapes &p_shapes) {
	RenderingServer *rs = RenderingServer::get_singleton();
	_ensure_overlay(p_root);
	rs->canvas_item_clear(overlay_item);
	const Ref<Font> font = ThemeDB::get_singleton()->get_fallback_font();
	const int size = ThemeDB::get_singleton()->get_fallback_font_size();

	const Camera3D *camera = p_root->get_camera_3d();
	if (camera != nullptr) {
		for (const Text3D &label : p_shapes.texts) {
			if (camera->is_position_behind(label.position)) {
				continue;
			}
			const Vector2 at = camera->unproject_position(label.position);
			const float width = font->get_string_size(label.text, HORIZONTAL_ALIGNMENT_LEFT, -1, size).x;
			const Vector2 left = at - Vector2(width * 0.5, 0);
			font->draw_string_outline(overlay_item, left, label.text, HORIZONTAL_ALIGNMENT_LEFT, -1, size, 4, Color(0, 0, 0, 0.75 * label.color.a));
			font->draw_string(overlay_item, left, label.text, HORIZONTAL_ALIGNMENT_LEFT, -1, size, label.color);
		}
	}

	if (values.is_empty()) {
		return;
	}
	const float line_height = font->get_height(size);
	const float margin = 8.0;
	float name_width = 0.0;
	float value_width = 0.0;
	for (const KeyValue<String, Value> &entry : values) {
		name_width = MAX(name_width, font->get_string_size(entry.key, HORIZONTAL_ALIGNMENT_LEFT, -1, size).x);
		value_width = MAX(value_width, font->get_string_size(entry.value.text, HORIZONTAL_ALIGNMENT_LEFT, -1, size).x);
	}
	const float gap = size;
	const Rect2 panel(Vector2(margin, margin), Vector2(name_width + gap + value_width + margin * 2, line_height * values.size() + margin * 2));
	rs->canvas_item_add_rect(overlay_item, panel, Color(0, 0, 0, 0.6));
	float y = margin * 2 + font->get_ascent(size);
	for (const KeyValue<String, Value> &entry : values) {
		font->draw_string(overlay_item, Vector2(margin * 2, y), entry.key, HORIZONTAL_ALIGNMENT_LEFT, -1, size, Color(1, 1, 1, 0.7));
		font->draw_string(overlay_item, Vector2(margin * 2 + name_width + gap, y), entry.value.text, HORIZONTAL_ALIGNMENT_LEFT, -1, size, Color(1, 1, 1));
		y += line_height;
	}
}

void DebugDraw::_shifted(const Vector3 &p_by) {
	MutexLock lock(mutex);
	frame_shapes.move(p_by);
	tick_shapes.move(p_by);
	for (Timed &batch : timed) {
		batch.shapes.move(p_by);
	}
}

void DebugDraw::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &DebugDraw::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &DebugDraw::is_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ClassDB::bind_method(D_METHOD("set_performance_overlay", "shown"), &DebugDraw::set_performance_overlay);
	ClassDB::bind_method(D_METHOD("is_performance_overlay"), &DebugDraw::is_performance_overlay);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "performance_overlay"), "set_performance_overlay", "is_performance_overlay");

	const Color white(1, 1, 1);
	ClassDB::bind_method(D_METHOD("line", "from", "to", "color", "duration"), &DebugDraw::line, DEFVAL(white), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("path", "points", "color", "duration", "closed"), &DebugDraw::path, DEFVAL(white), DEFVAL(0.0), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("arrow", "from", "to", "color", "duration"), &DebugDraw::arrow, DEFVAL(white), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("point", "position", "size", "color", "duration"), &DebugDraw::point, DEFVAL(0.25), DEFVAL(white), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("box", "transform", "color", "duration"), &DebugDraw::box, DEFVAL(white), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("aabb", "aabb", "color", "duration"), &DebugDraw::aabb, DEFVAL(white), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("sphere", "center", "radius", "color", "duration"), &DebugDraw::sphere, DEFVAL(0.5), DEFVAL(white), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("axes", "transform", "size", "duration"), &DebugDraw::axes, DEFVAL(1.0), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("text", "position", "text", "color", "duration"), &DebugDraw::text, DEFVAL(white), DEFVAL(0.0));

	ClassDB::bind_method(D_METHOD("line_2d", "from", "to", "color", "duration"), &DebugDraw::line_2d, DEFVAL(white), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("path_2d", "points", "color", "duration", "closed"), &DebugDraw::path_2d, DEFVAL(white), DEFVAL(0.0), DEFVAL(false));
	ClassDB::bind_method(D_METHOD("arrow_2d", "from", "to", "color", "duration"), &DebugDraw::arrow_2d, DEFVAL(white), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("rect_2d", "rect", "color", "duration"), &DebugDraw::rect_2d, DEFVAL(white), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("circle_2d", "center", "radius", "color", "duration"), &DebugDraw::circle_2d, DEFVAL(white), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("text_2d", "position", "text", "color", "duration"), &DebugDraw::text_2d, DEFVAL(white), DEFVAL(0.0));

	ClassDB::bind_method(D_METHOD("show_value", "name", "value", "duration"), &DebugDraw::show_value, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("clear"), &DebugDraw::clear);
	ClassDB::bind_method(D_METHOD("get_line_count"), &DebugDraw::get_line_count);
}
