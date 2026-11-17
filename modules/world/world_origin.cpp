#include "world_origin.h"

#include "../graphics/frame_budget.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/cpu_particles_3d.h"
#include "scene/3d/gpu_particles_3d.h"
#include "multimesh_body_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/3d/visual_instance_3d.h"
#include "scene/main/window.h"
#include "scene/main/scene_tree.h"
#include "servers/rendering/rendering_server.h"

WorldOrigin *WorldOrigin::singleton = nullptr;

WorldOrigin::WorldOrigin() {
	singleton = this;
	step = GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "world_origin/step", PROPERTY_HINT_RANGE, "1,100000,1,suffix:m"), 1000.0);
	calm_distance = GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "world_origin/calm_distance", PROPERTY_HINT_RANGE, "1,1000000,1,suffix:m"), 1500.0);
	max_distance = GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "world_origin/max_distance", PROPERTY_HINT_RANGE, "1,1000000,1,suffix:m"), 4000.0);
	// Declared before the renderer reads shader globals, so every shader can use it.
	Dictionary map_offset;
	map_offset["type"] = "vec3";
	map_offset["value"] = Vector3();
	GLOBAL_DEF(vformat("shader_globals/%s", SHADER_PARAMETER), map_offset);
	if (!Engine::get_singleton()->is_editor_hint()) {
		callable_mp(this, &WorldOrigin::_start).call_deferred();
	}
}

// Particle nodes are noted as they join the tree (and those already in it once), so a shift
// needn't look through every node.
void WorldOrigin::_start() {
	SceneTree *tree = SceneTree::get_singleton();
	if (started || tree == nullptr) {
		return;
	}
	started = true;
	tree->connect(SNAME("node_added"), callable_mp(this, &WorldOrigin::_node_added));
	for (const Variant &node : tree->get_root()->find_children("*", "GPUParticles3D", true, false)) {
		_node_added(Object::cast_to<Node>(node));
	}
	for (const Variant &node : tree->get_root()->find_children("*", "CPUParticles3D", true, false)) {
		_node_added(Object::cast_to<Node>(node));
	}
}

void WorldOrigin::_node_added(Node *p_node) {
	if (Object::cast_to<GPUParticles3D>(p_node) != nullptr || Object::cast_to<CPUParticles3D>(p_node) != nullptr) {
		particles.insert(p_node->get_instance_id());
	}
}

// Particles simulating in world space stay where they were when the world moves (Godot can't
// move them), so those under the world start afresh where they are; with a preprocess time
// set, nothing shows it.
void WorldOrigin::_restart_world_space_particles() {
	Node3D *world = _world();
	Vector<ObjectID> gone;
	for (const ObjectID &id : particles) {
		Node *node = ObjectDB::get_instance<Node>(id);
		if (node == nullptr) {
			gone.push_back(id);
			continue;
		}
		if (world == nullptr || !(world == node || world->is_ancestor_of(node))) {
			continue;
		}
		GPUParticles3D *gpu = Object::cast_to<GPUParticles3D>(node);
		CPUParticles3D *cpu = Object::cast_to<CPUParticles3D>(node);
		if (gpu != nullptr && !gpu->get_use_local_coordinates() && gpu->is_emitting()) {
			gpu->restart(true);
		} else if (cpu != nullptr && !cpu->get_use_local_coordinates() && cpu->is_emitting()) {
			cpu->restart(true);
		}
	}
	for (const ObjectID &id : gone) {
		particles.erase(id);
	}
}

WorldOrigin::~WorldOrigin() {
	singleton = nullptr;
}

// The node set as the world, else the current scene when it's 3D.
Node3D *WorldOrigin::_world() const {
	if (world_id.is_valid()) {
		return ObjectDB::get_instance<Node3D>(world_id);
	}
	SceneTree *tree = SceneTree::get_singleton();
	return tree != nullptr ? Object::cast_to<Node3D>(tree->get_current_scene()) : nullptr;
}

void WorldOrigin::set_world(Node3D *p_world) {
	world_id = p_world != nullptr ? p_world->get_instance_id() : ObjectID();
}

Node3D *WorldOrigin::get_world() const {
	return _world();
}

Vector3 WorldOrigin::get_offset() const {
	return offset;
}

void WorldOrigin::set_step(float p_step) {
	step = MAX(Math::round(p_step), 1.0f);
}

float WorldOrigin::get_step() const {
	return step;
}

void WorldOrigin::set_calm_distance(float p_distance) {
	calm_distance = MAX(p_distance, 0.0f);
}

float WorldOrigin::get_calm_distance() const {
	return calm_distance;
}

void WorldOrigin::set_max_distance(float p_distance) {
	max_distance = MAX(p_distance, 0.0f);
}

float WorldOrigin::get_max_distance() const {
	return max_distance;
}

Vector3 WorldOrigin::to_map(const Vector3 &p_global) const {
	return p_global + offset;
}

Vector3 WorldOrigin::to_global(const Vector3 &p_map) const {
	return p_map - offset;
}

Transform3D WorldOrigin::transform_to_map(const Transform3D &p_global) const {
	return Transform3D(p_global.basis, to_map(p_global.origin));
}

Transform3D WorldOrigin::transform_to_global(const Transform3D &p_map) const {
	return Transform3D(p_map.basis, to_global(p_map.origin));
}

// Shaders find map coordinates as the vertex's world position plus this.
void WorldOrigin::_publish_offset() {
	RenderingServer *rendering = RenderingServer::get_singleton();
	if (rendering == nullptr) {
		return;
	}
	rendering->global_shader_parameter_set(SHADER_PARAMETER, offset); // Declared as a shader global setting.
}

// Keeps a point (the player, or what the camera follows) near the origin: past calm_distance
// out on the ground at a calm moment, or past max_distance at any, the world shifts by whole
// steps to bring it back. Calm is whenever a shift's moment of work won't be seen: the player
// standing still, the screen dark.
void WorldOrigin::follow(const Vector3 &p_global, bool p_calm) {
	const float out = Vector2(p_global.x, p_global.z).length();
	if (out > max_distance || (p_calm && out > calm_distance)) {
		shift(-Vector3(Math::snapped(p_global.x, step), 0.0, Math::snapped(p_global.z, step)));
	}
}

// Moves the world node and everything under it. The servers move everything in its World3D at once
// (World3D::shift(): the renderer's instances and culling, the physics space), so the nodes under it,
// seeing the servers already have their new transforms, needn't send them one by one. Only the
// world node's own physics interpolation is reset (the rest follow it), so nothing is drawn sliding
// over a frame. Nodes in that World3D that don't move with it (outside it, or top-level) are put
// back where they are on the servers.
void WorldOrigin::_move_world(Node3D *p_world, const Vector3 &p_by) {
	const Ref<World3D> world_3d = p_world->is_inside_tree() ? p_world->get_world_3d() : Ref<World3D>();
	if (world_3d.is_valid()) {
		world_3d->shift(p_by);
	}
	p_world->set_position(p_world->get_position() + p_by);
	if (p_world->is_inside_tree()) {
		p_world->notification(Node::NOTIFICATION_RESET_PHYSICS_INTERPOLATION);
	}
	if (world_3d.is_valid() && SceneTree::get_singleton() != nullptr) {
		_put_back_unmoved(SceneTree::get_singleton()->get_root(), p_world, world_3d.ptr(), false);
	}
}

// Walks the tree: nodes that didn't move with the world (under no Node3D parent that did, or
// top-level) send their transforms again, as the servers moved them with everything else.
void WorldOrigin::_put_back_unmoved(Node *p_node, Node3D *p_world, World3D *p_world_3d, bool p_moved) {
	Node3D *spatial = Object::cast_to<Node3D>(p_node);
	bool moved = false;
	if (spatial != nullptr) {
		if (spatial == p_world) {
			moved = true;
		} else if (!spatial->is_set_as_top_level()) {
			moved = p_moved && Object::cast_to<Node3D>(spatial->get_parent()) != nullptr;
		}
		if (!moved && spatial->get_world_3d().ptr() == p_world_3d) {
			if (VisualInstance3D *visual = Object::cast_to<VisualInstance3D>(spatial)) {
				visual->update_server_transform();
			} else if (CollisionObject3D *body = Object::cast_to<CollisionObject3D>(spatial)) {
				body->update_server_transform();
			} else if (Object::cast_to<MultiMeshBody3D>(spatial) != nullptr) {
				spatial->notification(Node3D::NOTIFICATION_TRANSFORM_CHANGED);
			}
		}
	}
	for (int i = 0; i < p_node->get_child_count(true); i++) {
		_put_back_unmoved(p_node->get_child(i, true), p_world, p_world_3d, moved);
	}
}

// Moves the world, and everything under it, by whole metres.
void WorldOrigin::shift(const Vector3 &p_by) {
	const Vector3 by = p_by.round();
	if (by == Vector3()) {
		return;
	}
	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	Node3D *world = _world();
	if (world != nullptr) {
		_move_world(world, by);
		if (world->get_instance_id() != moved_id) {
			moved_id = world->get_instance_id();
			moved_by = Vector3();
		}
		moved_by += by;
	}
	offset -= by;
	_publish_offset();
	_restart_world_space_particles();
	emit_signal(SNAME("shifted"), by);
	if (FrameBudget::get_singleton() != nullptr) {
		FrameBudget::get_singleton()->measure("world origin shift", started);
	}
}

// Back to the map's own frame: no offset, and the world where it was before any shift (a world
// that was never shifted, such as a new one loading, stays where it is).
void WorldOrigin::reset() {
	Node3D *world = _world();
	const Vector3 by = world != nullptr && world->get_instance_id() == moved_id ? -moved_by : Vector3();
	if (by != Vector3()) {
		_move_world(world, by);
	}
	moved_id = ObjectID();
	moved_by = Vector3();
	offset = Vector3();
	_publish_offset();
	if (by != Vector3()) {
		emit_signal(SNAME("shifted"), by);
	}
}

void WorldOrigin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_world", "world"), &WorldOrigin::set_world);
	ClassDB::bind_method(D_METHOD("get_world"), &WorldOrigin::get_world);
	ClassDB::bind_method(D_METHOD("get_offset"), &WorldOrigin::get_offset);
	ClassDB::bind_method(D_METHOD("set_step", "metres"), &WorldOrigin::set_step);
	ClassDB::bind_method(D_METHOD("get_step"), &WorldOrigin::get_step);
	ClassDB::bind_method(D_METHOD("set_calm_distance", "metres"), &WorldOrigin::set_calm_distance);
	ClassDB::bind_method(D_METHOD("get_calm_distance"), &WorldOrigin::get_calm_distance);
	ClassDB::bind_method(D_METHOD("set_max_distance", "metres"), &WorldOrigin::set_max_distance);
	ClassDB::bind_method(D_METHOD("get_max_distance"), &WorldOrigin::get_max_distance);
	ClassDB::bind_method(D_METHOD("to_map", "global"), &WorldOrigin::to_map);
	ClassDB::bind_method(D_METHOD("to_global", "map"), &WorldOrigin::to_global);
	ClassDB::bind_method(D_METHOD("transform_to_map", "global"), &WorldOrigin::transform_to_map);
	ClassDB::bind_method(D_METHOD("transform_to_global", "map"), &WorldOrigin::transform_to_global);
	ClassDB::bind_method(D_METHOD("follow", "global", "calm"), &WorldOrigin::follow, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("shift", "by"), &WorldOrigin::shift);
	ClassDB::bind_method(D_METHOD("reset"), &WorldOrigin::reset);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "world", PROPERTY_HINT_NODE_TYPE, "Node3D", PROPERTY_USAGE_NONE), "set_world", "get_world");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "offset", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_offset");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "step", PROPERTY_HINT_NONE, "suffix:m"), "set_step", "get_step");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "calm_distance", PROPERTY_HINT_NONE, "suffix:m"), "set_calm_distance", "get_calm_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_distance", PROPERTY_HINT_NONE, "suffix:m"), "set_max_distance", "get_max_distance");

	ADD_SIGNAL(MethodInfo("shifted", PropertyInfo(Variant::VECTOR3, "by")));
}
