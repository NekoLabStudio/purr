#include "scatter_3d.h"

#include "impostor.h"

#include "multimesh_body_3d.h"
#include "world_origin.h"

#include "core/config/engine.h"
#include "core/math/random_pcg.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/multimesh.h"
#include "servers/physics_3d/direct_states/physics_direct_space_state_3d.h"

Scatter3D::Scatter3D() {
}

void Scatter3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			if (Engine::get_singleton()->is_editor_hint()) {
				set_notify_transform(true); // Moving it in the editor scatters it again.
			}
			_update_body();
			_update_far();
		} break;
		case NOTIFICATION_READY: {
			// Made in code rather than saved with a scene: scatter it now.
			if (!Engine::get_singleton()->is_editor_hint() && get_multimesh().is_null() && mesh.is_valid()) {
				scatter();
			}
		} break;
		case NOTIFICATION_TRANSFORM_CHANGED: {
			_queue_scatter();
		} break;
	}
}

// Only the editor scatters on its own; a game calls scatter() when it wants copies placed again.
void Scatter3D::_queue_scatter() {
	if (!is_inside_tree() || !Engine::get_singleton()->is_editor_hint() || scatter_queued) {
		return;
	}
	scatter_queued = true;
	callable_mp(this, &Scatter3D::_scatter_queued).call_deferred();
}

void Scatter3D::_scatter_queued() {
	scatter_queued = false;
	if (is_inside_tree()) {
		scatter();
	}
}

void Scatter3D::_find_terrains(Node *p_node, Vector<Node *> &r_terrains) {
	if (p_node->is_class("Terrain3D")) {
		r_terrains.push_back(p_node);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_find_terrains(p_node->get_child(i), r_terrains);
	}
}

// The first surface between two points: a physics body on surface_mask, or else a terrain, which
// has no collision in the editor (and in a game, only round the camera), so is asked its height.
bool Scatter3D::_find_surface(PhysicsDirectSpaceState3D *p_space, const Vector<Node *> &p_terrains, const Vector3 &p_from, const Vector3 &p_to, Vector3 &r_position, Vector3 &r_normal) const {
	PhysicsServer3DTypes::RayParameters parameters;
	parameters.from = p_from;
	parameters.to = p_to;
	parameters.collision_mask = surface_mask;
	PhysicsServer3DTypes::RayResult result;
	if (p_space != nullptr && p_space->intersect_ray(parameters, result)) {
		r_position = result.position;
		r_normal = result.normal;
		return true;
	}

	// A terrain's data is in map positions (the same as global ones until the world shifts).
	const Vector3 offset = WorldOrigin::get_singleton() != nullptr ? WorldOrigin::get_singleton()->get_offset() : Vector3();
	const real_t top = MAX(p_from.y, p_to.y);
	const real_t bottom = MIN(p_from.y, p_to.y);
	for (Node *terrain : p_terrains) {
		Object *data = terrain->call("get_data");
		if (data == nullptr) {
			continue;
		}
		const real_t height = real_t(data->call("get_height", p_from + offset)) - offset.y;
		if (Math::is_nan(height) || height > top || height < bottom) {
			continue;
		}
		r_position = Vector3(p_from.x, height, p_from.z);
		r_normal = data->call("get_normal", r_position + offset);
		if (!r_normal.is_finite()) {
			r_normal = Vector3(0, 1, 0);
		}
		return true;
	}
	return false;
}

// Places the copies again: one try for every square metre's worth of density, each dropped from
// the top of the box onto the first surface below it.
void Scatter3D::scatter() {
	ERR_FAIL_COND_MSG(!is_inside_tree(), "Scatter3D must be in the scene tree to scatter.");
	if (body != nullptr) {
		body->set_multimesh(Ref<MultiMesh>()); // So rays don't land on the last scatter's copies.
	}

	Ref<MultiMesh> multimesh;
	multimesh.instantiate();
	multimesh->set_transform_format(MultiMesh::TRANSFORM_3D);
	multimesh->set_mesh(mesh);

	if (mesh.is_valid()) {
		const Ref<World3D> world = get_world_3d();
		PhysicsDirectSpaceState3D *space = world.is_valid() ? world->get_direct_space_state() : nullptr;
		const Transform3D global = get_global_transform();
		const Transform3D to_local = global.affine_inverse();
		const int tries = MIN(int(size.x * size.z * density), MAX_COPIES);
		const real_t slope_limit = Math::deg_to_rad(max_slope);

		// Terrains are found once: a copy with no body below it asks them for their height.
		Vector<Node *> terrains;
		Node *root = get_tree()->get_edited_scene_root() != nullptr ? get_tree()->get_edited_scene_root() : get_tree()->get_root();
		_find_terrains(root, terrains);

		RandomPCG random{ uint64_t(seed) };
		Vector<Transform3D> placed;
		for (int i = 0; i < tries; i++) {
			// Every try draws the same numbers whether it lands or not, so a copy stays put as others
			// come and go.
			const Vector3 spot = Vector3((random.randf() - 0.5) * size.x, size.y * 0.5, (random.randf() - 0.5) * size.z);
			const real_t turn = random.randf() * Math::TAU;
			const real_t scale = Math::lerp(scale_min, scale_max, random.randf());

			Vector3 position;
			Vector3 normal;
			if (!_find_surface(space, terrains, global.xform(spot), global.xform(spot - Vector3(0, size.y, 0)), position, normal)) {
				continue;
			}
			if (normal.angle_to(Vector3(0, 1, 0)) > slope_limit) {
				continue;
			}
			const Vector3 up = Vector3(0, 1, 0).lerp(normal, align_to_surface).normalized();
			Basis basis = Basis(Quaternion(Vector3(0, 1, 0), up));
			if (random_rotation) {
				basis = basis * Basis(Vector3(0, 1, 0), turn);
			}
			placed.push_back(to_local * Transform3D(basis.scaled_local(Vector3(scale, scale, scale)), position));
		}

		// Set whole, as a loaded MultiMesh's is: quicker than a copy at a time.
		Vector<float> buffer;
		buffer.resize(placed.size() * 12);
		float *write = buffer.ptrw();
		for (const Transform3D &transform : placed) {
			for (int row = 0; row < 3; row++) {
				*write++ = transform.basis.rows[row].x;
				*write++ = transform.basis.rows[row].y;
				*write++ = transform.basis.rows[row].z;
				*write++ = transform.origin[row];
			}
		}
		multimesh->set_instance_count(placed.size());
		multimesh->set_buffer(buffer);
	}

	set_multimesh(multimesh);
	_update_body();
	_update_far();
}

int Scatter3D::get_count() const {
	return get_multimesh().is_valid() ? get_multimesh()->get_instance_count() : 0;
}

// A MultiMeshBody3D child gives the copies collision, when there's a shape for them.
void Scatter3D::_update_body() {
	if (collision_shape.is_null()) {
		if (body != nullptr) {
			body->queue_free();
			body = nullptr;
		}
		return;
	}
	if (body == nullptr) {
		body = memnew(MultiMeshBody3D);
		body->set_collision_mask(0);
		add_child(body, false, INTERNAL_MODE_BACK); // Not saved: made again from the properties.
	}
	body->set_shape(collision_shape);
	body->set_collision_layer(collision_layer);
	body->set_multimesh(get_multimesh());
}

// Past impostor_distance, the copies are drawn as the impostor: a second MultiMesh of the same
// transforms, with the impostor's quad, and the copies themselves no further than that.
void Scatter3D::_update_far() {
	const bool wanted = impostor.is_valid() && impostor->get_texture().is_valid() && impostor_distance > 0.0 && get_multimesh().is_valid();
	if (!wanted) {
		if (far != nullptr) {
			far->queue_free();
			far = nullptr;
			set_visibility_range_end(0.0);
		}
		return;
	}
	if (far == nullptr) {
		far = memnew(MultiMeshInstance3D);
		add_child(far, false, INTERNAL_MODE_BACK); // Not saved: made again from the properties.
	}
	const Ref<MultiMesh> near = get_multimesh();
	Ref<MultiMesh> copies;
	copies.instantiate();
	copies->set_transform_format(MultiMesh::TRANSFORM_3D);
	copies->set_mesh(impostor->get_mesh());
	copies->set_instance_count(near->get_instance_count());
	copies->set_buffer(near->get_buffer());
	far->set_multimesh(copies);
	far->set_cast_shadows_setting(get_cast_shadows_setting());
	far->set_layer_mask(get_layer_mask());
	// Each takes over from the other across a few metres, fading.
	const float margin = MAX(impostor_distance * 0.05f, 1.0f);
	set_visibility_range_end(impostor_distance + margin);
	set_visibility_range_end_margin(margin);
	set_visibility_range_fade_mode(VISIBILITY_RANGE_FADE_SELF);
	far->set_visibility_range_begin(impostor_distance - margin);
	far->set_visibility_range_begin_margin(margin);
	far->set_visibility_range_fade_mode(VISIBILITY_RANGE_FADE_SELF);
}

void Scatter3D::set_impostor(const Ref<Impostor> &p_impostor) {
	impostor = p_impostor;
	_update_far();
}

Ref<Impostor> Scatter3D::get_impostor() const {
	return impostor;
}

void Scatter3D::set_impostor_distance(float p_distance) {
	impostor_distance = MAX(p_distance, 0.0f);
	_update_far();
}

float Scatter3D::get_impostor_distance() const {
	return impostor_distance;
}

void Scatter3D::_validate_property(PropertyInfo &p_property) const {
	if (p_property.name == "multimesh") {
		p_property.usage = PROPERTY_USAGE_STORAGE; // Made by scattering: saved, not edited.
	}
}

void Scatter3D::set_mesh(const Ref<Mesh> &p_mesh) {
	mesh = p_mesh;
	_queue_scatter();
}

Ref<Mesh> Scatter3D::get_mesh() const {
	return mesh;
}

void Scatter3D::set_size(const Vector3 &p_size) {
	size = p_size.maxf(0.01);
	update_gizmos();
	_queue_scatter();
}

Vector3 Scatter3D::get_size() const {
	return size;
}

void Scatter3D::set_density(float p_density) {
	density = MAX(p_density, 0.0f);
	_queue_scatter();
}

float Scatter3D::get_density() const {
	return density;
}

void Scatter3D::set_seed(int p_seed) {
	seed = p_seed;
	_queue_scatter();
}

int Scatter3D::get_seed() const {
	return seed;
}

void Scatter3D::set_scale_min(float p_scale) {
	scale_min = MAX(p_scale, 0.01f);
	_queue_scatter();
}

float Scatter3D::get_scale_min() const {
	return scale_min;
}

void Scatter3D::set_scale_max(float p_scale) {
	scale_max = MAX(p_scale, 0.01f);
	_queue_scatter();
}

float Scatter3D::get_scale_max() const {
	return scale_max;
}

void Scatter3D::set_random_rotation(bool p_random) {
	random_rotation = p_random;
	_queue_scatter();
}

bool Scatter3D::is_random_rotation() const {
	return random_rotation;
}

void Scatter3D::set_align_to_surface(float p_amount) {
	align_to_surface = CLAMP(p_amount, 0.0f, 1.0f);
	_queue_scatter();
}

float Scatter3D::get_align_to_surface() const {
	return align_to_surface;
}

void Scatter3D::set_max_slope(float p_degrees) {
	max_slope = CLAMP(p_degrees, 0.0f, 90.0f);
	_queue_scatter();
}

float Scatter3D::get_max_slope() const {
	return max_slope;
}

void Scatter3D::set_surface_mask(uint32_t p_mask) {
	surface_mask = p_mask;
	_queue_scatter();
}

uint32_t Scatter3D::get_surface_mask() const {
	return surface_mask;
}

void Scatter3D::set_collision_shape(const Ref<Shape3D> &p_shape) {
	collision_shape = p_shape;
	if (is_inside_tree()) {
		_update_body();
	}
}

Ref<Shape3D> Scatter3D::get_collision_shape() const {
	return collision_shape;
}

void Scatter3D::set_collision_layer(uint32_t p_layer) {
	collision_layer = p_layer;
	if (body != nullptr) {
		body->set_collision_layer(collision_layer);
	}
}

uint32_t Scatter3D::get_collision_layer() const {
	return collision_layer;
}

void Scatter3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mesh", "mesh"), &Scatter3D::set_mesh);
	ClassDB::bind_method(D_METHOD("get_mesh"), &Scatter3D::get_mesh);
	ClassDB::bind_method(D_METHOD("set_size", "size"), &Scatter3D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &Scatter3D::get_size);
	ClassDB::bind_method(D_METHOD("set_density", "density"), &Scatter3D::set_density);
	ClassDB::bind_method(D_METHOD("get_density"), &Scatter3D::get_density);
	ClassDB::bind_method(D_METHOD("set_seed", "seed"), &Scatter3D::set_seed);
	ClassDB::bind_method(D_METHOD("get_seed"), &Scatter3D::get_seed);
	ClassDB::bind_method(D_METHOD("set_scale_min", "scale"), &Scatter3D::set_scale_min);
	ClassDB::bind_method(D_METHOD("get_scale_min"), &Scatter3D::get_scale_min);
	ClassDB::bind_method(D_METHOD("set_scale_max", "scale"), &Scatter3D::set_scale_max);
	ClassDB::bind_method(D_METHOD("get_scale_max"), &Scatter3D::get_scale_max);
	ClassDB::bind_method(D_METHOD("set_random_rotation", "random"), &Scatter3D::set_random_rotation);
	ClassDB::bind_method(D_METHOD("is_random_rotation"), &Scatter3D::is_random_rotation);
	ClassDB::bind_method(D_METHOD("set_align_to_surface", "amount"), &Scatter3D::set_align_to_surface);
	ClassDB::bind_method(D_METHOD("get_align_to_surface"), &Scatter3D::get_align_to_surface);
	ClassDB::bind_method(D_METHOD("set_max_slope", "degrees"), &Scatter3D::set_max_slope);
	ClassDB::bind_method(D_METHOD("get_max_slope"), &Scatter3D::get_max_slope);
	ClassDB::bind_method(D_METHOD("set_surface_mask", "mask"), &Scatter3D::set_surface_mask);
	ClassDB::bind_method(D_METHOD("get_surface_mask"), &Scatter3D::get_surface_mask);
	ClassDB::bind_method(D_METHOD("set_collision_shape", "shape"), &Scatter3D::set_collision_shape);
	ClassDB::bind_method(D_METHOD("get_collision_shape"), &Scatter3D::get_collision_shape);
	ClassDB::bind_method(D_METHOD("set_collision_layer", "layer"), &Scatter3D::set_collision_layer);
	ClassDB::bind_method(D_METHOD("get_collision_layer"), &Scatter3D::get_collision_layer);
	ClassDB::bind_method(D_METHOD("scatter"), &Scatter3D::scatter);
	ClassDB::bind_method(D_METHOD("get_count"), &Scatter3D::get_count);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"), "set_mesh", "get_mesh");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "density", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:/m²"), "set_density", "get_density");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
	ADD_GROUP("Placement", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scale_min", PROPERTY_HINT_RANGE, "0.01,4,0.01,or_greater"), "set_scale_min", "get_scale_min");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scale_max", PROPERTY_HINT_RANGE, "0.01,4,0.01,or_greater"), "set_scale_max", "get_scale_max");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "random_rotation"), "set_random_rotation", "is_random_rotation");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "align_to_surface", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_align_to_surface", "get_align_to_surface");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_slope", PROPERTY_HINT_RANGE, "0,90,0.1,degrees"), "set_max_slope", "get_max_slope");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "surface_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_surface_mask", "get_surface_mask");
	ClassDB::bind_method(D_METHOD("set_impostor", "impostor"), &Scatter3D::set_impostor);
	ClassDB::bind_method(D_METHOD("get_impostor"), &Scatter3D::get_impostor);
	ClassDB::bind_method(D_METHOD("set_impostor_distance", "distance"), &Scatter3D::set_impostor_distance);
	ClassDB::bind_method(D_METHOD("get_impostor_distance"), &Scatter3D::get_impostor_distance);
	ADD_GROUP("Far away", "impostor");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "impostor", PROPERTY_HINT_RESOURCE_TYPE, "Impostor"), "set_impostor", "get_impostor");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "impostor_distance", PROPERTY_HINT_RANGE, "0,2000,0.1,or_greater,suffix:m"), "set_impostor_distance", "get_impostor_distance");
	ADD_GROUP("Collision", "collision_");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "collision_shape", PROPERTY_HINT_RESOURCE_TYPE, "Shape3D"), "set_collision_shape", "get_collision_shape");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_layer", "get_collision_layer");
}
