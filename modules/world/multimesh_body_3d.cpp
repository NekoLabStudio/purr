#include "multimesh_body_3d.h"

#include "../graphics/frame_budget.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/templates/sort_array.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/physics_3d/physics_server_3d.h"
#include "servers/rendering/rendering_server.h"

LocalVector<ObjectID> MultiMeshBody3D::queue;
bool MultiMeshBody3D::running = false;

MultiMeshBody3D::MultiMeshBody3D() {
	set_notify_transform(true);
}

MultiMeshBody3D::~MultiMeshBody3D() {
	_clear();
}

void MultiMeshBody3D::_clear() {
	PhysicsServer3D *physics = PhysicsServer3D::get_singleton();
	for (const RID &body : bodies) {
		physics->free_rid(body);
	}
	bodies.clear();
	body_index.clear();
	order.clear();
	transforms.clear();
	built = 0;
	if (queued) {
		queue.erase(get_instance_id());
		queued = false;
	}
}

// Where shapes are made first: round the main viewport's camera.
Vector3 MultiMeshBody3D::_camera_position() {
	SceneTree *tree = SceneTree::get_singleton();
	Camera3D *camera = tree != nullptr && tree->get_root() != nullptr ? tree->get_root()->get_camera_3d() : nullptr;
	return camera != nullptr && camera->is_inside_tree() ? camera->get_global_position() : Vector3();
}

// Starts again from the nearest instance: for a changed MultiMesh, shape or offset.
void MultiMeshBody3D::rebuild() {
	_clear();
	if (!is_inside_tree() || multimesh.is_null() || shape.is_null() || multimesh->get_instance_count() == 0) {
		return;
	}
	_read_instances();
	queue.push_back(get_instance_id());
	queued = true;
	if (running) {
		return;
	}
	running = true;
	const float budget_ms = GLOBAL_GET("physics/3d/multimesh_collision_budget_ms");
	FrameBudget *frame_budget = FrameBudget::get_singleton();
	if (frame_budget != nullptr) {
		frame_budget->spread("multimesh collision", callable_mp_static(&MultiMeshBody3D::_build_queued), budget_ms);
	} else {
		while (_build_queued()) {
		}
	}
}

// The instances' transforms, read from the renderer once and whole (per instance, each read asks
// the renderer), and their order, nearest the camera first.
void MultiMeshBody3D::_read_instances() {
	const int count = multimesh->get_instance_count();
	const Vector<float> buffer = RenderingServer::get_singleton()->multimesh_get_buffer(multimesh->get_rid());
	const int stride = 12 + (multimesh->is_using_colors() ? 4 : 0) + (multimesh->is_using_custom_data() ? 4 : 0);
	const bool readable = multimesh->get_transform_format() == MultiMesh::TRANSFORM_3D && buffer.size() >= count * stride;
	transforms.resize(count);
	for (int i = 0; i < count; i++) {
		if (!readable) {
			transforms.write[i] = multimesh->get_instance_transform(i);
			continue;
		}
		// Each instance's basis's rows, each followed by the origin's part.
		const float *at = buffer.ptr() + i * stride;
		Transform3D &transform = transforms.write[i];
		transform.basis.rows[0] = Vector3(at[0], at[1], at[2]);
		transform.origin.x = at[3];
		transform.basis.rows[1] = Vector3(at[4], at[5], at[6]);
		transform.origin.y = at[7];
		transform.basis.rows[2] = Vector3(at[8], at[9], at[10]);
		transform.origin.z = at[11];
	}
	struct Nearer {
		const Vector<Transform3D> *transforms = nullptr;
		Vector3 eye;
		bool operator()(int p_a, int p_b) const {
			return (*transforms)[p_a].origin.distance_squared_to(eye) < (*transforms)[p_b].origin.distance_squared_to(eye);
		}
	};
	order.resize(count);
	for (int i = 0; i < count; i++) {
		order.write[i] = i;
	}
	SortArray<int, Nearer> sorter;
	sorter.compare.transforms = &transforms;
	sorter.compare.eye = to_local(_camera_position());
	sorter.sort(order.ptrw(), count);
}

// One slice from the queue, of the node nearest the camera: true while there's more to make.
bool MultiMeshBody3D::_build_queued() {
	const Vector3 eye = _camera_position();
	int nearest = -1;
	float nearest_distance = Math::INF;
	for (uint32_t i = 0; i < queue.size();) {
		MultiMeshBody3D *node = ObjectDB::get_instance<MultiMeshBody3D>(queue[i]);
		if (node == nullptr || !node->is_inside_tree() || !node->can_process()) {
			if (node != nullptr) {
				node->queued = false;
			}
			queue.remove_at_unordered(i);
			continue;
		}
		const float distance = node->get_global_position().distance_squared_to(eye);
		if (distance < nearest_distance) {
			nearest_distance = distance;
			nearest = i;
		}
		i++;
	}
	if (nearest >= 0) {
		MultiMeshBody3D *node = ObjectDB::get_instance<MultiMeshBody3D>(queue[nearest]);
		if (!node->_build_slice()) {
			node->queued = false;
			queue.remove_at_unordered(nearest);
		}
	}
	running = !queue.is_empty();
	return running;
}

// One body's worth of shapes: true while there are more to make.
bool MultiMeshBody3D::_build_slice() {
	PhysicsServer3D *physics = PhysicsServer3D::get_singleton();
	const RID body = physics->body_create();
	physics->body_set_mode(body, PS3DE::BODY_MODE_STATIC);
	physics->body_attach_object_instance_id(body, get_instance_id());
	physics->body_set_collision_layer(body, collision_layer);
	physics->body_set_collision_mask(body, collision_mask);
	physics->body_set_state(body, PS3DE::BODY_STATE_TRANSFORM, get_global_transform());
	const int end = MIN(built + SHAPES_A_BODY, order.size());
	for (int i = built; i < end; i++) {
		physics->body_add_shape(body, shape->get_rid(), transforms[order[i]] * shape_offset);
	}
	physics->body_set_space(body, get_world_3d()->get_space());
	body_index.insert(body, bodies.size());
	bodies.push_back(body);
	built = end;
	if (built < order.size()) {
		return true;
	}
	transforms.clear();
	return false;
}

void MultiMeshBody3D::_place_bodies() {
	PhysicsServer3D *physics = PhysicsServer3D::get_singleton();
	const Transform3D at = get_global_transform();
	for (const RID &body : bodies) {
		physics->body_set_state(body, PS3DE::BODY_STATE_TRANSFORM, at);
	}
}

// The MultiMesh instance a collision was with, from the collider's body and shape (as a raycast
// or contact reports them), or -1.
int MultiMeshBody3D::get_instance(const RID &p_body, int p_shape) const {
	const int *index = body_index.getptr(p_body);
	if (index == nullptr || p_shape < 0 || p_shape >= SHAPES_A_BODY) {
		return -1;
	}
	const int made = *index * SHAPES_A_BODY + p_shape;
	return made < built ? order[made] : -1;
}

bool MultiMeshBody3D::is_built() const {
	return multimesh.is_valid() && shape.is_valid() && !queued && built == multimesh->get_instance_count();
}

void MultiMeshBody3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_WORLD: {
			if (!is_built()) {
				rebuild(); // Never built, or left the tree partway.
			} else {
				const RID space = get_world_3d()->get_space();
				for (const RID &body : bodies) {
					PhysicsServer3D::get_singleton()->body_set_space(body, space);
				}
				_place_bodies();
			}
		} break;
		case NOTIFICATION_EXIT_WORLD: {
			for (const RID &body : bodies) {
				PhysicsServer3D::get_singleton()->body_set_space(body, RID());
			}
		} break;
		case NOTIFICATION_TRANSFORM_CHANGED: {
			_place_bodies();
		} break;
		// Switched off (as a CollisionObject3D is, by its process mode), out of the physics world.
		case NOTIFICATION_DISABLED: {
			disabled = true;
			for (const RID &body : bodies) {
				PhysicsServer3D::get_singleton()->body_set_space(body, RID());
			}
		} break;
		case NOTIFICATION_ENABLED: {
			if (!disabled) {
				break; // Only back from being switched off.
			}
			disabled = false;
			if (!is_built()) {
				rebuild();
			} else if (is_inside_tree()) {
				const RID space = get_world_3d()->get_space();
				for (const RID &body : bodies) {
					PhysicsServer3D::get_singleton()->body_set_space(body, space);
				}
			}
		} break;
	}
}

void MultiMeshBody3D::set_multimesh(const Ref<MultiMesh> &p_multimesh) {
	multimesh = p_multimesh;
	rebuild();
}

Ref<MultiMesh> MultiMeshBody3D::get_multimesh() const {
	return multimesh;
}

void MultiMeshBody3D::set_shape(const Ref<Shape3D> &p_shape) {
	shape = p_shape;
	rebuild();
}

Ref<Shape3D> MultiMeshBody3D::get_shape() const {
	return shape;
}

void MultiMeshBody3D::set_shape_offset(const Transform3D &p_offset) {
	shape_offset = p_offset;
	rebuild();
}

Transform3D MultiMeshBody3D::get_shape_offset() const {
	return shape_offset;
}

void MultiMeshBody3D::set_collision_layer(uint32_t p_layer) {
	collision_layer = p_layer;
	for (const RID &body : bodies) {
		PhysicsServer3D::get_singleton()->body_set_collision_layer(body, p_layer);
	}
}

uint32_t MultiMeshBody3D::get_collision_layer() const {
	return collision_layer;
}

void MultiMeshBody3D::set_collision_mask(uint32_t p_mask) {
	collision_mask = p_mask;
	for (const RID &body : bodies) {
		PhysicsServer3D::get_singleton()->body_set_collision_mask(body, p_mask);
	}
}

uint32_t MultiMeshBody3D::get_collision_mask() const {
	return collision_mask;
}

void MultiMeshBody3D::_bind_methods() {
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "physics/3d/multimesh_collision_budget_ms", PROPERTY_HINT_RANGE, "0.1,20,0.1,suffix:ms"), 1.0);

	ClassDB::bind_method(D_METHOD("set_multimesh", "multimesh"), &MultiMeshBody3D::set_multimesh);
	ClassDB::bind_method(D_METHOD("get_multimesh"), &MultiMeshBody3D::get_multimesh);
	ClassDB::bind_method(D_METHOD("set_shape", "shape"), &MultiMeshBody3D::set_shape);
	ClassDB::bind_method(D_METHOD("get_shape"), &MultiMeshBody3D::get_shape);
	ClassDB::bind_method(D_METHOD("set_shape_offset", "offset"), &MultiMeshBody3D::set_shape_offset);
	ClassDB::bind_method(D_METHOD("get_shape_offset"), &MultiMeshBody3D::get_shape_offset);
	ClassDB::bind_method(D_METHOD("set_collision_layer", "layer"), &MultiMeshBody3D::set_collision_layer);
	ClassDB::bind_method(D_METHOD("get_collision_layer"), &MultiMeshBody3D::get_collision_layer);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &MultiMeshBody3D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &MultiMeshBody3D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("rebuild"), &MultiMeshBody3D::rebuild);
	ClassDB::bind_method(D_METHOD("is_built"), &MultiMeshBody3D::is_built);
	ClassDB::bind_method(D_METHOD("get_instance", "body", "shape"), &MultiMeshBody3D::get_instance);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "multimesh", PROPERTY_HINT_RESOURCE_TYPE, "MultiMesh"), "set_multimesh", "get_multimesh");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "shape", PROPERTY_HINT_RESOURCE_TYPE, "Shape3D"), "set_shape", "get_shape");
	ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "shape_offset"), "set_shape_offset", "get_shape_offset");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_layer", "get_collision_layer");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");
}
