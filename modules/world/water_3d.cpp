#include "water_3d.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/mesh.h"

#include "water_material.h"
#include "world_origin.h"

namespace {
// The endless sea: a grid this many vertices across, reaching this far, finest in the middle.
constexpr int SEA_VERTICES = 192;
constexpr float SEA_REACH = 4000.0;
constexpr float SEA_FINENESS = 2.2; // How much finer the middle is than the edge.
constexpr float SEA_STEP = 4.0; // It moves with the camera in steps this long.
constexpr int MOST_LAKE_DIVISIONS = 512;
} // namespace

Water3D::Water3D() {
	Ref<WaterMaterial> water;
	water.instantiate();
	own_material = water;
}

void Water3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (instance == nullptr) {
				instance = memnew(MeshInstance3D);
				instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
				add_child(instance, false, INTERNAL_MODE_BACK);
				_build();
			}
			set_process_internal(size == Vector2() && !Engine::get_singleton()->is_editor_hint());
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_follow_camera();
		} break;
	}
}

Ref<Material> Water3D::_material() const {
	return material.is_valid() ? material : own_material;
}

void Water3D::_build() {
	if (instance == nullptr) {
		return;
	}
	if (size != Vector2()) {
		Ref<PlaneMesh> plane;
		plane.instantiate();
		plane->set_size(size);
		plane->set_subdivide_width(MIN(int(size.x / vertex_spacing), MOST_LAKE_DIVISIONS));
		plane->set_subdivide_depth(MIN(int(size.y / vertex_spacing), MOST_LAKE_DIVISIONS));
		instance->set_mesh(plane);
		instance->set_as_top_level(false);
		instance->set_transform(Transform3D());
	} else {
		// Each row and column further out than the last by more.
		PackedVector3Array vertices;
		PackedVector3Array normals;
		PackedInt32Array indices;
		const int n = SEA_VERTICES;
		LocalVector<float> along;
		for (int i = 0; i <= n; i++) {
			const float u = float(i) / n * 2.0f - 1.0f;
			along.push_back(SIGN(u) * Math::pow(Math::abs(u), SEA_FINENESS) * SEA_REACH);
		}
		for (int z = 0; z <= n; z++) {
			for (int x = 0; x <= n; x++) {
				vertices.push_back(Vector3(along[x], 0.0, along[z]));
				normals.push_back(Vector3(0, 1, 0));
			}
		}
		for (int z = 0; z < n; z++) {
			for (int x = 0; x < n; x++) {
				const int a = z * (n + 1) + x;
				indices.append_array({ a, a + 1, a + n + 1, a + 1, a + n + 2, a + n + 1 });
			}
		}
		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = vertices;
		arrays[Mesh::ARRAY_NORMAL] = normals;
		arrays[Mesh::ARRAY_INDEX] = indices;
		Ref<ArrayMesh> sea;
		sea.instantiate();
		sea->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		instance->set_mesh(sea);
		instance->set_as_top_level(true);
	}
	instance->set_material_override(_material());
	// The waves rise above the flat mesh.
	const Ref<WaterMaterial> water = _material();
	instance->set_extra_cull_margin(water.is_valid() ? water->get_wave_height() * 2.0f + 1.0f : 1.0f);
	_follow_camera();
}

void Water3D::_follow_camera() {
	if (instance == nullptr || size != Vector2() || !is_inside_tree()) {
		return;
	}
	const Camera3D *camera = get_viewport() != nullptr ? get_viewport()->get_camera_3d() : nullptr;
	const Vector3 at = camera != nullptr ? camera->get_global_position() : get_global_position();
	const Vector3 snapped = Vector3(Math::snapped(at.x, SEA_STEP), get_global_position().y, Math::snapped(at.z, SEA_STEP));
	if (!instance->get_global_position().is_equal_approx(snapped)) {
		instance->set_global_transform(Transform3D(Basis(), snapped));
	}
}

float Water3D::get_height_at(const Vector3 &p_position) const {
	const float still = is_inside_tree() ? get_global_position().y : get_position().y;
	const Ref<WaterMaterial> water = _material();
	if (water.is_null()) {
		return still;
	}
	const Vector3 offset = WorldOrigin::get_singleton() != nullptr ? WorldOrigin::get_singleton()->get_offset() : Vector3();
	return still + water->get_height_at(Vector2(p_position.x + offset.x, p_position.z + offset.z));
}

void Water3D::set_size(const Vector2 &p_size) {
	size = p_size.max(Vector2());
	_build();
	if (is_inside_tree()) {
		set_process_internal(size == Vector2() && !Engine::get_singleton()->is_editor_hint());
	}
}

Vector2 Water3D::get_size() const {
	return size;
}

void Water3D::set_vertex_spacing(float p_metres) {
	vertex_spacing = MAX(p_metres, 0.05f);
	_build();
}

float Water3D::get_vertex_spacing() const {
	return vertex_spacing;
}

void Water3D::set_material(const Ref<Material> &p_material) {
	material = p_material;
	_build();
}

Ref<Material> Water3D::get_material() const {
	return material;
}

void Water3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_size", "size"), &Water3D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &Water3D::get_size);
	ClassDB::bind_method(D_METHOD("set_vertex_spacing", "metres"), &Water3D::set_vertex_spacing);
	ClassDB::bind_method(D_METHOD("get_vertex_spacing"), &Water3D::get_vertex_spacing);
	ClassDB::bind_method(D_METHOD("set_material", "material"), &Water3D::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &Water3D::get_material);
	ClassDB::bind_method(D_METHOD("get_height_at", "position"), &Water3D::get_height_at);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "vertex_spacing", PROPERTY_HINT_RANGE, "0.05,16,0.01,or_greater,suffix:m"), "set_vertex_spacing", "get_vertex_spacing");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_material", "get_material");
}
