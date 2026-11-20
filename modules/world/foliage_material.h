#pragma once

// FoliageMaterial: a material for grass, leaves and plants that sway in the wind. Each copy leans
// with the wind, bends more the higher up it is, catches gusts that roll across the world, and
// flutters a little on its own; the wind is Weather3D's (a light breeze without one). Plants bend
// away from FoliageBender3Ds nearby, and Weather3D's snow settles on them. Leaves are drawn from
// both sides, cut out by their texture's alpha, and lit through from behind.

#include "core/os/mutex.h"
#include "scene/resources/material.h"
#include "scene/resources/texture.h"

class FoliageMaterial : public Material {
	GDCLASS(FoliageMaterial, Material);

	Color albedo_color = Color(1, 1, 1);
	Ref<Texture2D> albedo_texture;
	float alpha_scissor_threshold = 0.5;
	Ref<Texture2D> normal_texture;
	float normal_strength = 1.0;
	float roughness = 0.8;
	Color backlight = Color(0.35, 0.45, 0.2);
	float sway = 1.0;
	float sway_height = 1.0;
	float stiffness = 1.0;

	static Mutex shader_mutex;
	static RID shader;
	mutable bool shader_set = false;
	static void _update_shader();
	void _set_texture(const StringName &p_param, const Ref<Texture2D> &p_texture);

protected:
	static void _bind_methods();

public:
	void set_albedo_color(const Color &p_color);
	Color get_albedo_color() const;
	void set_albedo_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_albedo_texture() const;
	void set_alpha_scissor_threshold(float p_threshold);
	float get_alpha_scissor_threshold() const;
	void set_normal_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_normal_texture() const;
	void set_normal_strength(float p_strength);
	float get_normal_strength() const;
	void set_roughness(float p_roughness);
	float get_roughness() const;
	void set_backlight(const Color &p_color);
	Color get_backlight() const;
	void set_sway(float p_sway);
	float get_sway() const;
	void set_sway_height(float p_height);
	float get_sway_height() const;
	void set_stiffness(float p_stiffness);
	float get_stiffness() const;

	virtual Shader::Mode get_shader_mode() const override;
	virtual RID get_shader_rid() const override;
	virtual RID get_rid() const override;

	static void cleanup_shader();

	FoliageMaterial();
};
