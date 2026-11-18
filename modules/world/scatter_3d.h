#pragma once

// Scatter3D: copies of a mesh scattered over whatever is below a box (the ground, rocks, a
// terrain), drawn as one MultiMesh. Copies land where the surface isn't too steep, turned and
// sized at random within limits; the same seed scatters them the same way. In the editor it
// scatters again as it's changed or moved, and the result is saved with the scene, so a game
// doesn't redo it. A collision shape makes the copies solid, through a MultiMeshBody3D.

#include "scene/3d/multimesh_instance_3d.h"
#include "scene/resources/3d/shape_3d.h"
#include "scene/resources/mesh.h"

class MultiMeshBody3D;
class PhysicsDirectSpaceState3D;

class Scatter3D : public MultiMeshInstance3D {
	GDCLASS(Scatter3D, MultiMeshInstance3D);

	Ref<Mesh> mesh;
	Vector3 size = Vector3(20, 10, 20);
	float density = 1.0;
	int seed = 0;
	float scale_min = 1.0;
	float scale_max = 1.0;
	bool random_rotation = true;
	float align_to_surface = 0.0;
	float max_slope = 45.0;
	uint32_t surface_mask = 1;
	Ref<Shape3D> collision_shape;
	uint32_t collision_layer = 1;

	MultiMeshBody3D *body = nullptr;
	bool scatter_queued = false;

	void _queue_scatter();
	void _scatter_queued();
	void _update_body();
	bool _find_surface(PhysicsDirectSpaceState3D *p_space, const Vector<Node *> &p_terrains, const Vector3 &p_from, const Vector3 &p_to, Vector3 &r_position, Vector3 &r_normal) const;
	static void _find_terrains(Node *p_node, Vector<Node *> &r_terrains);

protected:
	void _notification(int p_what);
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();

public:
	static constexpr int MAX_COPIES = 100000;

	void set_mesh(const Ref<Mesh> &p_mesh);
	Ref<Mesh> get_mesh() const;
	void set_size(const Vector3 &p_size);
	Vector3 get_size() const;
	void set_density(float p_density);
	float get_density() const;
	void set_seed(int p_seed);
	int get_seed() const;
	void set_scale_min(float p_scale);
	float get_scale_min() const;
	void set_scale_max(float p_scale);
	float get_scale_max() const;
	void set_random_rotation(bool p_random);
	bool is_random_rotation() const;
	void set_align_to_surface(float p_amount);
	float get_align_to_surface() const;
	void set_max_slope(float p_degrees);
	float get_max_slope() const;
	void set_surface_mask(uint32_t p_mask);
	uint32_t get_surface_mask() const;
	void set_collision_shape(const Ref<Shape3D> &p_shape);
	Ref<Shape3D> get_collision_shape() const;
	void set_collision_layer(uint32_t p_layer);
	uint32_t get_collision_layer() const;

	void scatter();
	int get_count() const;

	Scatter3D();
};
