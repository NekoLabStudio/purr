#pragma once

// WaterMaterial: water for any mesh: a lake's plane, a river's ribbon, the sea under Water3D. Its
// waves roll along the wind (Weather3D's, unless given a direction), what's under it is seen
// through it, bent and fading into its own colour with depth, foam gathers along shores and on
// steep crests, rain ripples it, and a river's surface flows. Waves stay put as the world
// shifts. get_height_at() gives the waves' height on the CPU, for things that float.

#include "core/os/mutex.h"
#include "scene/resources/material.h"
#include "scene/resources/texture.h"

class WaterMaterial : public Material {
	GDCLASS(WaterMaterial, Material);

	Color color = Color(0.55, 0.82, 0.78);
	Color deep_color = Color(0.01, 0.05, 0.07);
	float clarity = 3.0;
	float wave_height = 0.3;
	float wave_length = 10.0;
	Vector2 wave_direction = Vector2(1.0, 0.3);
	bool follow_wind = true;
	float choppiness = 0.5;
	Vector2 flow;
	float foam = 0.6;
	float roughness = 0.03;
	float refraction = 0.03;

	static Mutex shader_mutex;
	static RID shader;
	static Ref<Texture2D> ripples_a;
	static Ref<Texture2D> ripples_b;
	mutable bool shader_set = false;
	static void _update_shader();
	Vector2 _direction() const;
	float _strength() const;

protected:
	static void _bind_methods();

public:
	void set_color(const Color &p_color);
	Color get_color() const;
	void set_deep_color(const Color &p_color);
	Color get_deep_color() const;
	void set_clarity(float p_metres);
	float get_clarity() const;
	void set_wave_height(float p_metres);
	float get_wave_height() const;
	void set_wave_length(float p_metres);
	float get_wave_length() const;
	void set_wave_direction(const Vector2 &p_direction);
	Vector2 get_wave_direction() const;
	void set_follow_wind(bool p_follow);
	bool is_following_wind() const;
	void set_choppiness(float p_choppiness);
	float get_choppiness() const;
	void set_flow(const Vector2 &p_flow);
	Vector2 get_flow() const;
	void set_foam(float p_foam);
	float get_foam() const;
	void set_roughness(float p_roughness);
	float get_roughness() const;
	void set_refraction(float p_refraction);
	float get_refraction() const;

	// The waves' height above the still surface at a map position (global plus WorldOrigin's
	// offset), now: what the vertex shader moves the surface to.
	float get_height_at(const Vector2 &p_map_position) const;

	virtual Shader::Mode get_shader_mode() const override;
	virtual RID get_shader_rid() const override;
	virtual RID get_rid() const override;

	static void cleanup_shader();

	WaterMaterial();
};
