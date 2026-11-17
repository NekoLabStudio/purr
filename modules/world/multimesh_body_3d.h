#pragma once

// MultiMeshBody3D: collision for a MultiMesh's instances, which Godot's MultiMesh doesn't have. One
// shape per instance, placed by its transform, in static bodies of a few dozen. Every
// MultiMeshBody3D's bodies are made from one queue a slice at a time within one budget a frame
// (physics/3d/multimesh_collision_budget_ms), the node nearest the camera first and its nearest
// instances first, so a streamed-in world's props never hold up a frame and the road ahead is
// solid before the skyline.

#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/3d/shape_3d.h"
#include "scene/resources/multimesh.h"

class MultiMeshBody3D : public Node3D {
	GDCLASS(MultiMeshBody3D, Node3D);

	Ref<MultiMesh> multimesh;
	Ref<Shape3D> shape;
	Transform3D shape_offset;
	uint32_t collision_layer = 1;
	uint32_t collision_mask = 1;

	Vector<RID> bodies; // Each holds SHAPES_A_BODY instances' shapes, in the order below.
	HashMap<RID, int> body_index;
	Vector<int> order; // The instances, nearest the camera first when the build began.
	Vector<Transform3D> transforms; // Theirs, read once a build.
	int built = 0; // Instances with a shape so far.
	bool queued = false;
	bool disabled = false;

	static LocalVector<ObjectID> queue; // Nodes with shapes still to make.
	static bool running;

	void _clear();
	void _read_instances();
	bool _build_slice();
	void _place_bodies();
	static Vector3 _camera_position();
	static bool _build_queued();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static constexpr int SHAPES_A_BODY = 64;

	void set_multimesh(const Ref<MultiMesh> &p_multimesh);
	Ref<MultiMesh> get_multimesh() const;
	void set_shape(const Ref<Shape3D> &p_shape);
	Ref<Shape3D> get_shape() const;
	void set_shape_offset(const Transform3D &p_offset);
	Transform3D get_shape_offset() const;
	void set_collision_layer(uint32_t p_layer);
	uint32_t get_collision_layer() const;
	void set_collision_mask(uint32_t p_mask);
	uint32_t get_collision_mask() const;

	void rebuild();
	bool is_built() const;
	int get_instance(const RID &p_body, int p_shape) const;

	MultiMeshBody3D();
	~MultiMeshBody3D();
};
