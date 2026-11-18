#pragma once

// InstanceGroup3D: many copies of a mesh that move (a crowd, units, bullets, a flock), drawn in
// chunks of the world. The game gives every copy's transform in one call a frame; the copies are
// sorted into a grid of chunk_size cells, each drawn as its own MultiMesh. So copies off screen or
// out of range cost nothing, and Godot's mesh detail, chosen per MultiMesh, is chosen per chunk:
// far chunks draw the simpler mesh. One MultiMeshInstance3D is culled and detailed as one box,
// everything at the detail the nearest copy needs. Changes in a frame are drawn together, once.

#include "core/templates/hash_map.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/visual_instance_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"

class InstanceGroup3D : public Node3D {
	GDCLASS(InstanceGroup3D, Node3D);

	struct Chunk {
		RID multimesh;
		RID instance;
		int capacity = 0; // Copies its buffer has room for; what it draws is set apart.
	};

	Ref<Mesh> mesh;
	Ref<Material> material_override;
	GeometryInstance3D::ShadowCastingSetting cast_shadow = GeometryInstance3D::SHADOW_CASTING_SETTING_ON;
	float visibility_range_end = 0.0;
	uint32_t layers = 1;
	float chunk_size = 64.0;
	bool use_colors = false;
	bool use_custom_data = false;

	PackedFloat32Array transforms; // 12 floats a copy, as a MultiMesh's buffer has them.
	PackedColorArray colors;
	PackedColorArray custom_data;
	HashMap<Vector3i, Chunk> chunks;
	bool update_queued = false;

	void _queue_update();
	void _update();
	void _free_chunks();
	void _apply_settings(const Chunk &p_chunk) const;
	int _stride() const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_mesh(const Ref<Mesh> &p_mesh);
	Ref<Mesh> get_mesh() const;
	void set_material_override(const Ref<Material> &p_material);
	Ref<Material> get_material_override() const;
	void set_cast_shadows_setting(GeometryInstance3D::ShadowCastingSetting p_setting);
	GeometryInstance3D::ShadowCastingSetting get_cast_shadows_setting() const;
	void set_layer_mask(uint32_t p_mask);
	uint32_t get_layer_mask() const;
	void set_visibility_range_end(float p_distance);
	float get_visibility_range_end() const;
	void set_chunk_size(float p_size);
	float get_chunk_size() const;
	void set_use_colors(bool p_use);
	bool is_using_colors() const;
	void set_use_custom_data(bool p_use);
	bool is_using_custom_data() const;

	void set_transforms(const PackedFloat32Array &p_transforms);
	PackedFloat32Array get_transforms() const;
	void set_colors(const PackedColorArray &p_colors);
	PackedColorArray get_colors() const;
	void set_custom_data(const PackedColorArray &p_custom_data);
	PackedColorArray get_custom_data() const;
	int get_count() const;
	int get_chunk_count() const;

	~InstanceGroup3D();
};
