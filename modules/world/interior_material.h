#pragma once

// InteriorMaterial: windows with rooms behind them, and no geometry inside: each window's room is
// worked out per pixel from where the camera looks in (interior mapping), with its own walls,
// floor, ceiling, blinds and furniture. At night (TimeOfDay's), some rooms' lights come on, a few
// at a time; by day they're dim behind the glass's reflections. Laid on a building's front, its
// UVs set the windows: rooms across and down each UV square, in a facade texture whose alpha
// cuts out the glass.

#include "core/os/mutex.h"
#include "scene/resources/material.h"
#include "scene/resources/texture.h"

class InteriorMaterial : public Material {
	GDCLASS(InteriorMaterial, Material);

	Vector2i rooms = Vector2i(1, 1);
	float room_depth = 1.0;
	Ref<Texture2D> facade_texture;
	float frame_width = 0.04;
	Color frame_color = Color(0.2, 0.2, 0.22);
	float lit_rooms = 0.6;
	Color light_color = Color(1.0, 0.82, 0.6);
	float light_energy = 0.6;

	static Mutex shader_mutex;
	static RID shader;
	mutable bool shader_set = false;
	static void _update_shader();

protected:
	static void _bind_methods();

public:
	void set_rooms(const Vector2i &p_rooms);
	Vector2i get_rooms() const;
	void set_room_depth(float p_depth);
	float get_room_depth() const;
	void set_facade_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_facade_texture() const;
	void set_frame_width(float p_width);
	float get_frame_width() const;
	void set_frame_color(const Color &p_color);
	Color get_frame_color() const;
	void set_lit_rooms(float p_fraction);
	float get_lit_rooms() const;
	void set_light_color(const Color &p_color);
	Color get_light_color() const;
	void set_light_energy(float p_energy);
	float get_light_energy() const;

	virtual Shader::Mode get_shader_mode() const override;
	virtual RID get_shader_rid() const override;
	virtual RID get_rid() const override;

	static void cleanup_shader();

	InteriorMaterial();
};
