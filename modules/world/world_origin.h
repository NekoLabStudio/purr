#pragma once

// WorldOrigin: a floating origin. Far from where a world's coordinates start, 32-bit positions
// step in millimetres and what's near the camera shimmers, so the world shifts back under the
// player in whole steps (a kilometre by default), keeping everything near (0, 0, 0). Map
// coordinates (what a game saves and sends) are global ones plus offset. Shifts are whole metres,
// so offset itself is exact in single precision for any world on Earth's scale.

#include "core/object/object.h"
#include "core/object/object_id.h"
#include "core/math/transform_3d.h"
#include "core/templates/hash_set.h"

class Node;
class Node3D;
class World3D;

class WorldOrigin : public Object {
	GDCLASS(WorldOrigin, Object);

	static WorldOrigin *singleton;

	ObjectID world_id;
	Vector3 offset;
	// What shifts have moved the world node by, so reset() undoes them on that node only (a new
	// world loading after another was never moved).
	ObjectID moved_id;
	Vector3 moved_by;
	float step = 1000.0;
	float calm_distance = 1500.0;
	float max_distance = 4000.0;

	// Particle nodes seen (simulating in world space or not: that can change), restarted by a shift.
	HashSet<ObjectID> particles;
	bool started = false;

	Node3D *_world() const;
	void _start();
	void _node_added(Node *p_node);
	void _restart_world_space_particles();
	void _move_world(Node3D *p_world, const Vector3 &p_by);
	static void _after_move(Node *p_node, Node3D *p_world, World3D *p_world_3d, const Vector3 &p_by, bool p_moved);
	void _publish_offset();

protected:
	static void _bind_methods();

public:
	static WorldOrigin *get_singleton() { return singleton; }

	static constexpr const char *SHADER_PARAMETER = "map_offset";

	void set_world(Node3D *p_world);
	Node3D *get_world() const;
	Vector3 get_offset() const;
	void set_step(float p_step);
	float get_step() const;
	void set_calm_distance(float p_distance);
	float get_calm_distance() const;
	void set_max_distance(float p_distance);
	float get_max_distance() const;

	Vector3 to_map(const Vector3 &p_global) const;
	Vector3 to_global(const Vector3 &p_map) const;
	Transform3D transform_to_map(const Transform3D &p_global) const;
	Transform3D transform_to_global(const Transform3D &p_map) const;

	void follow(const Vector3 &p_global, bool p_calm = true);
	void shift(const Vector3 &p_by);
	void reset();

	WorldOrigin();
	~WorldOrigin();
};
