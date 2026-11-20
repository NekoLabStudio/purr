#pragma once

// BakedAnimationMaterial: draws a BakedAnimations' mesh moving through its animations, each copy
// of an InstanceGroup3D (or MultiMesh) in the animation and at the moment its custom data says
// (BakedAnimations.get_custom_data()), on the GPU, without skeletons.

#include "core/os/mutex.h"
#include "scene/resources/material.h"
#include "scene/resources/texture.h"

#include "baked_animations.h"

class BakedAnimationMaterial : public Material {
	GDCLASS(BakedAnimationMaterial, Material);

	Ref<BakedAnimations> baked_animations;
	Color albedo_color = Color(1, 1, 1);
	Ref<Texture2D> albedo_texture;
	float roughness = 0.8;

	static Mutex shader_mutex;
	static RID shader;
	mutable bool shader_set = false;
	static void _update_shader();
	void _baked_changed();

protected:
	static void _bind_methods();

public:
	void set_baked_animations(const Ref<BakedAnimations> &p_baked);
	Ref<BakedAnimations> get_baked_animations() const;
	void set_albedo_color(const Color &p_color);
	Color get_albedo_color() const;
	void set_albedo_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_albedo_texture() const;
	void set_roughness(float p_roughness);
	float get_roughness() const;

	virtual Shader::Mode get_shader_mode() const override;
	virtual RID get_shader_rid() const override;
	virtual RID get_rid() const override;

	static void cleanup_shader();

	BakedAnimationMaterial();
};
