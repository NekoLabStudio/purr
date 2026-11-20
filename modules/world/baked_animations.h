#pragma once

// BakedAnimations: a skinned character's animations recorded vertex by vertex into textures, so
// thousands of copies can be animated on the GPU without skeletons: an InstanceGroup3D draws
// `mesh` with a BakedAnimationMaterial, and each copy's custom data says which animation it's in
// and how far along (get_custom_data()). Made with bake(), in the editor or as the game loads.

#include "core/io/resource.h"
#include "scene/resources/mesh.h"
#include "scene/resources/texture.h"

class AnimationPlayer;
class Node;

class BakedAnimations : public Resource {
	GDCLASS(BakedAnimations, Resource);

	Ref<ArrayMesh> mesh; // The character as one surface, without its bones.
	Ref<Texture2D> positions; // Each frame's vertex positions, rows_per_frame rows a frame.
	Ref<Texture2D> normals;
	int texture_width = 0;
	int rows_per_frame = 0;
	// Each animation: its first frame, how many frames, frames a second, and whether it loops.
	Dictionary animations;

protected:
	static void _bind_methods();

public:
	Error bake(Node *p_character, AnimationPlayer *p_player, const PackedStringArray &p_animations = PackedStringArray(), float p_frames_per_second = 30.0);

	Color get_custom_data(const StringName &p_animation, float p_time) const;
	float get_length(const StringName &p_animation) const;
	PackedStringArray get_animation_names() const;

	void set_mesh(const Ref<ArrayMesh> &p_mesh);
	Ref<ArrayMesh> get_mesh() const;
	void set_positions(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_positions() const;
	void set_normals(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_normals() const;
	void set_texture_width(int p_width);
	int get_texture_width() const;
	void set_rows_per_frame(int p_rows);
	int get_rows_per_frame() const;
	void set_animations(const Dictionary &p_animations);
	Dictionary get_animations() const;
};
