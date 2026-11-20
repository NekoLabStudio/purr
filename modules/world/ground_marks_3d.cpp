#include "ground_marks_3d.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/mesh.h"
#include "servers/physics_3d/physics_server_3d.h"

#include "world_origin.h"

Ref<StandardMaterial3D> GroundMarks3D::default_material;

namespace {
// A mark is fully there for most of its life, then fades over this last part of it.
constexpr float FADING = 0.3;
// Marks fading are drawn again this often (seconds).
constexpr double FADE_STEP = 0.2;
// Lifted off the ground this far, so it doesn't flicker into it.
constexpr float LIFT = 0.01;
} // namespace

void GroundMarks3D::_notification(int p_what) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (instance == nullptr) {
				mesh.instantiate();
				instance = memnew(MeshInstance3D);
				instance->set_as_top_level(true);
				instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
				instance->set_mesh(mesh);
				add_child(instance, false, INTERNAL_MODE_BACK);
			}
			if (WorldOrigin::get_singleton() != nullptr) {
				WorldOrigin::get_singleton()->connect(SNAME("shifted"), callable_mp(this, &GroundMarks3D::_shifted));
			}
			gap = true;
			set_process_internal(true);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (WorldOrigin::get_singleton() != nullptr && WorldOrigin::get_singleton()->is_connected(SNAME("shifted"), callable_mp(this, &GroundMarks3D::_shifted))) {
				WorldOrigin::get_singleton()->disconnect(SNAME("shifted"), callable_mp(this, &GroundMarks3D::_shifted));
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			clock += get_process_delta_time();
			const Vector3 at = get_global_transform_interpolated().origin;
			if (emitting) {
				const float apart = at.distance_to(last_position);
				if (gap || apart >= spacing) {
					Vector3 ground;
					Vector3 normal;
					if (_ground(at, ground, normal)) {
						Vector3 forward = gap ? -get_global_basis().get_column(Vector3::AXIS_Z) : at - last_position;
						forward = (forward - normal * forward.dot(normal));
						if (forward.length_squared() < 1e-8) {
							forward = normal.get_any_perpendicular();
						}
						_add(ground + normal * LIFT, normal, forward.normalized());
						last_position = at;
						gap = false;
					} else {
						gap = true; // Off the ground: the strip breaks.
					}
				}
			} else {
				gap = true;
			}
			// Those that have had their time go.
			uint32_t expired = 0;
			while (expired < marks.size() && clock - marks[expired].born > lifetime) {
				expired++;
			}
			if (expired > 0) {
				for (uint32_t i = 0; i + expired < marks.size(); i++) {
					marks[i] = marks[i + expired];
				}
				marks.resize(marks.size() - expired);
				changed = true;
			}
			const bool fading = !marks.is_empty() && clock - marks[0].born > lifetime * (1.0f - FADING);
			if (changed || (fading && clock - drawn_at > FADE_STEP)) {
				_draw();
			}
		} break;
	}
}

// The surface under a point, not counting the body the marks are left by.
bool GroundMarks3D::_ground(const Vector3 &p_from, Vector3 &r_position, Vector3 &r_normal) const {
	PhysicsDirectSpaceState3D *space = is_inside_tree() ? get_world_3d()->get_direct_space_state() : nullptr;
	if (space == nullptr) {
		return false;
	}
	PhysicsServer3DTypes::RayParameters ray;
	ray.from = p_from + Vector3(0.0, 0.2, 0.0);
	ray.to = p_from - Vector3(0.0, ground_distance, 0.0);
	ray.collision_mask = collision_mask;
	for (const Node *node = this; node != nullptr; node = node->get_parent()) {
		if (const CollisionObject3D *own = Object::cast_to<CollisionObject3D>(node)) {
			ray.exclude.insert(own->get_rid());
		}
	}
	PhysicsServer3DTypes::RayResult hit;
	if (!space->intersect_ray(ray, hit)) {
		return false;
	}
	r_position = hit.position;
	r_normal = hit.normal.normalized();
	return true;
}

void GroundMarks3D::_add(const Vector3 &p_position, const Vector3 &p_normal, const Vector3 &p_forward) {
	if (int(marks.size()) >= max_marks) {
		for (uint32_t i = 1; i < marks.size(); i++) {
			marks[i - 1] = marks[i];
		}
		marks.resize(marks.size() - 1);
		if (!marks.is_empty()) {
			marks[0].starts = true;
		}
	}
	Mark mark;
	mark.position = p_position;
	mark.normal = p_normal;
	mark.forward = p_forward;
	mark.born = clock;
	mark.starts = gap || marks.is_empty();
	marks.push_back(mark);
	changed = true;
}

void GroundMarks3D::_draw() {
	changed = false;
	drawn_at = clock;
	mesh->clear_surfaces();
	if (marks.is_empty()) {
		return;
	}
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedColorArray colours;
	PackedVector2Array uvs;
	PackedInt32Array indices;
	const float half = width * 0.5f;
	float along = 0.0f;
	for (uint32_t i = 0; i < marks.size(); i++) {
		const Mark &mark = marks[i];
		const float age = float(clock - mark.born) / MAX(lifetime, 0.001f);
		const float alpha = CLAMP((1.0f - age) / FADING, 0.0f, 1.0f);
		const Color colour(1.0, 1.0, 1.0, alpha);
		const Vector3 side = mark.normal.cross(mark.forward).normalized() * half;
		const int first = vertices.size();
		// The oldest left begins a strip, whatever came before it.
		const bool starts = mark.starts || i == 0;
		if (mode == MODE_STRIP) {
			along = starts ? 0.0f : along + mark.position.distance_to(marks[i - 1].position) / MAX(width, 0.001f);
			vertices.push_back(mark.position - side);
			vertices.push_back(mark.position + side);
			uvs.push_back(Vector2(0.0, along));
			uvs.push_back(Vector2(1.0, along));
			for (int v = 0; v < 2; v++) {
				normals.push_back(mark.normal);
				colours.push_back(colour);
			}
			if (!starts) {
				indices.append_array({ first - 2, first, first - 1, first - 1, first, first + 1 });
			}
		} else {
			const Vector3 ahead = mark.forward * length * 0.5f;
			vertices.append_array({ mark.position - side + ahead, mark.position + side + ahead, mark.position - side - ahead, mark.position + side - ahead });
			uvs.append_array({ Vector2(0, 0), Vector2(1, 0), Vector2(0, 1), Vector2(1, 1) });
			for (int v = 0; v < 4; v++) {
				normals.push_back(mark.normal);
				colours.push_back(colour);
			}
			indices.append_array({ first, first + 2, first + 1, first + 1, first + 2, first + 3 });
		}
	}
	if (indices.is_empty()) {
		return; // One strip mark alone: nothing between it and the next yet.
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_COLOR] = colours;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_INDEX] = indices;
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	mesh->surface_set_material(0, material.is_valid() ? material : _default_material());
}

// Dark, a little see-through, and faded by each mark's colour.
Ref<Material> GroundMarks3D::_default_material() {
	if (default_material.is_null()) {
		default_material.instantiate();
		default_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		default_material->set_albedo(Color(0.04, 0.04, 0.04, 0.6));
		default_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		default_material->set_roughness(0.9);
		default_material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	}
	return default_material;
}

void GroundMarks3D::cleanup() {
	default_material.unref();
}

void GroundMarks3D::_shifted(const Vector3 &p_by) {
	for (Mark &mark : marks) {
		mark.position += p_by;
	}
	last_position += p_by;
	changed = true;
}

void GroundMarks3D::clear() {
	marks.clear();
	gap = true;
	changed = true;
}

int GroundMarks3D::get_mark_count() const {
	return marks.size();
}

void GroundMarks3D::set_emitting(bool p_emitting) {
	emitting = p_emitting;
}

bool GroundMarks3D::is_emitting() const {
	return emitting;
}

void GroundMarks3D::set_mode(Mode p_mode) {
	mode = p_mode;
	changed = true;
	notify_property_list_changed();
}

GroundMarks3D::Mode GroundMarks3D::get_mode() const {
	return mode;
}

void GroundMarks3D::set_width(float p_width) {
	width = MAX(p_width, 0.001f);
	changed = true;
}

float GroundMarks3D::get_width() const {
	return width;
}

void GroundMarks3D::set_spacing(float p_spacing) {
	spacing = MAX(p_spacing, 0.01f);
}

float GroundMarks3D::get_spacing() const {
	return spacing;
}

void GroundMarks3D::set_length(float p_length) {
	length = MAX(p_length, 0.001f);
	changed = true;
}

float GroundMarks3D::get_length() const {
	return length;
}

void GroundMarks3D::set_lifetime(float p_seconds) {
	lifetime = MAX(p_seconds, 0.01f);
}

float GroundMarks3D::get_lifetime() const {
	return lifetime;
}

void GroundMarks3D::set_max_marks(int p_marks) {
	max_marks = MAX(p_marks, 2);
}

int GroundMarks3D::get_max_marks() const {
	return max_marks;
}

void GroundMarks3D::set_ground_distance(float p_metres) {
	ground_distance = MAX(p_metres, 0.0f);
}

float GroundMarks3D::get_ground_distance() const {
	return ground_distance;
}

void GroundMarks3D::set_collision_mask(uint32_t p_mask) {
	collision_mask = p_mask;
}

uint32_t GroundMarks3D::get_collision_mask() const {
	return collision_mask;
}

void GroundMarks3D::set_material(const Ref<Material> &p_material) {
	material = p_material;
	changed = true;
}

Ref<Material> GroundMarks3D::get_material() const {
	return material;
}

void GroundMarks3D::_validate_property(PropertyInfo &p_property) const {
	if (mode == MODE_STRIP && p_property.name == "length") {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

void GroundMarks3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_emitting", "emitting"), &GroundMarks3D::set_emitting);
	ClassDB::bind_method(D_METHOD("is_emitting"), &GroundMarks3D::is_emitting);
	ClassDB::bind_method(D_METHOD("set_mode", "mode"), &GroundMarks3D::set_mode);
	ClassDB::bind_method(D_METHOD("get_mode"), &GroundMarks3D::get_mode);
	ClassDB::bind_method(D_METHOD("set_width", "width"), &GroundMarks3D::set_width);
	ClassDB::bind_method(D_METHOD("get_width"), &GroundMarks3D::get_width);
	ClassDB::bind_method(D_METHOD("set_spacing", "spacing"), &GroundMarks3D::set_spacing);
	ClassDB::bind_method(D_METHOD("get_spacing"), &GroundMarks3D::get_spacing);
	ClassDB::bind_method(D_METHOD("set_length", "length"), &GroundMarks3D::set_length);
	ClassDB::bind_method(D_METHOD("get_length"), &GroundMarks3D::get_length);
	ClassDB::bind_method(D_METHOD("set_lifetime", "seconds"), &GroundMarks3D::set_lifetime);
	ClassDB::bind_method(D_METHOD("get_lifetime"), &GroundMarks3D::get_lifetime);
	ClassDB::bind_method(D_METHOD("set_max_marks", "marks"), &GroundMarks3D::set_max_marks);
	ClassDB::bind_method(D_METHOD("get_max_marks"), &GroundMarks3D::get_max_marks);
	ClassDB::bind_method(D_METHOD("set_ground_distance", "metres"), &GroundMarks3D::set_ground_distance);
	ClassDB::bind_method(D_METHOD("get_ground_distance"), &GroundMarks3D::get_ground_distance);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &GroundMarks3D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &GroundMarks3D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_material", "material"), &GroundMarks3D::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &GroundMarks3D::get_material);
	ClassDB::bind_method(D_METHOD("get_mark_count"), &GroundMarks3D::get_mark_count);
	ClassDB::bind_method(D_METHOD("clear"), &GroundMarks3D::clear);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "emitting"), "set_emitting", "is_emitting");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Strip,Stamps"), "set_mode", "get_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "width", PROPERTY_HINT_RANGE, "0.001,4,0.001,or_greater,suffix:m"), "set_width", "get_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "length", PROPERTY_HINT_RANGE, "0.001,4,0.001,or_greater,suffix:m"), "set_length", "get_length");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spacing", PROPERTY_HINT_RANGE, "0.01,4,0.01,or_greater,suffix:m"), "set_spacing", "get_spacing");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lifetime", PROPERTY_HINT_RANGE, "0.01,600,0.01,or_greater,suffix:s"), "set_lifetime", "get_lifetime");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_marks", PROPERTY_HINT_RANGE, "2,4096,1,or_greater"), "set_max_marks", "get_max_marks");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_material", "get_material");
	ADD_GROUP("Ground", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ground_distance", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:m"), "set_ground_distance", "get_ground_distance");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");

	BIND_ENUM_CONSTANT(MODE_STRIP);
	BIND_ENUM_CONSTANT(MODE_STAMPS);
}
