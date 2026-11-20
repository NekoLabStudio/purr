#pragma once

// Impostor: a node (a tree, a rock, a building) baked from many directions over the upper half of
// a sphere into one texture, drawn far away as a single quad turned to the camera
// (ImpostorMaterial), at a fraction of the node's cost. Baked with bake(), which renders, so in
// the editor or as the game loads, not headless.

#include "core/io/resource.h"
#include "scene/resources/mesh.h"
#include "scene/resources/texture.h"

class Camera3D;
class ImpostorMaterial;
class Node3D;
class SubViewport;

// A bake under way: one view a frame or two, then the texture.
class ImpostorBake : public RefCounted {
	GDCLASS(ImpostorBake, RefCounted);

	friend class Impostor;
	Ref<class Impostor> impostor;
	Ref<ImpostorBake> running; // Itself, held while it bakes: nothing else holds it but the awaiting code's signal.
	SubViewport *view = nullptr;
	Camera3D *camera = nullptr;
	Ref<Image> atlas;
	int grid = 8;
	int tile = 128;
	int cell = 0;
	int waited = 0;

	void _place_camera();
	void _frame_drawn();
	void _finish(const Dictionary &p_result);

protected:
	static void _bind_methods();
};

class Impostor : public Resource {
	GDCLASS(Impostor, Resource);

	Ref<Texture2D> texture;
	int grid = 8;
	Vector3 center;
	float radius = 1.0;
	mutable Ref<Mesh> mesh;
	mutable Ref<ImpostorMaterial> material;

	static Vector3 _direction(int p_grid, int p_cell);

protected:
	static void _bind_methods();

public:
	Signal bake(Node3D *p_node, int p_grid = 8, int p_tile_size = 128);
	Ref<Mesh> get_mesh() const;

	void set_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture() const;
	void set_grid(int p_grid);
	int get_grid() const;
	void set_center(const Vector3 &p_center);
	Vector3 get_center() const;
	void set_radius(float p_radius);
	float get_radius() const;

	friend class ImpostorBake;
};
