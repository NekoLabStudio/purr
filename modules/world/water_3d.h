#pragma once

// Water3D: a body of water: a lake or pond of a given size, or with no size an endless sea round
// the camera, its grid finest close by. It's drawn with a WaterMaterial (its own unless given
// one), and get_height_at() says where the surface is, waves and all, for things that float.

#include "scene/3d/node_3d.h"
#include "scene/resources/material.h"

class MeshInstance3D;

class Water3D : public Node3D {
	GDCLASS(Water3D, Node3D);

	Vector2 size;
	float vertex_spacing = 1.0;
	Ref<Material> material;
	Ref<Material> own_material; // When it has none of its own.
	MeshInstance3D *instance = nullptr;

	void _build();
	void _follow_camera();
	Ref<Material> _material() const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_size(const Vector2 &p_size);
	Vector2 get_size() const;
	void set_vertex_spacing(float p_metres);
	float get_vertex_spacing() const;
	void set_material(const Ref<Material> &p_material);
	Ref<Material> get_material() const;

	// The surface's global height above or below a global position, now.
	float get_height_at(const Vector3 &p_position) const;

	Water3D();
};
