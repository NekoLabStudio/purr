#pragma once

// ImpostorMaterial: draws an Impostor, a node baked from many directions into one texture, as a
// single quad turned to the camera, showing the view baked nearest the camera's direction.

#include "core/os/mutex.h"
#include "scene/resources/material.h"
#include "scene/resources/texture.h"

class ImpostorMaterial : public Material {
	GDCLASS(ImpostorMaterial, Material);

	Ref<Texture2D> texture;
	int grid = 8;
	Vector3 center;
	float radius = 1.0;
	float alpha_scissor_threshold = 0.5;

	static Mutex shader_mutex;
	static RID shader;
	mutable bool shader_set = false;
	static void _update_shader();

protected:
	static void _bind_methods();

public:
	void set_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_texture() const;
	void set_grid(int p_grid);
	int get_grid() const;
	void set_center(const Vector3 &p_center);
	Vector3 get_center() const;
	void set_radius(float p_radius);
	float get_radius() const;
	void set_alpha_scissor_threshold(float p_threshold);
	float get_alpha_scissor_threshold() const;

	virtual Shader::Mode get_shader_mode() const override;
	virtual RID get_shader_rid() const override;
	virtual RID get_rid() const override;

	static void cleanup_shader();

	ImpostorMaterial();
};
