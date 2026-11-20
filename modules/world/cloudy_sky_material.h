#pragma once

// CloudySkyMaterial: a sky whose colours follow the sun through day, dusk and night, with the sun's
// disc, high thin cirrus and a layer of clouds lit by the sun, and a grey deck when it's overcast.
// It follows TimeOfDay's sun (even below the horizon, for dusk) and Weather3D's clouds and wind,
// through shader globals, so the sky's reflections aren't drawn again each frame as clouds drift.

#include "core/os/mutex.h"
#include "scene/resources/material.h"

class CloudySkyMaterial : public Material {
	GDCLASS(CloudySkyMaterial, Material);

	Color day_top_color = Color(0.25, 0.46, 0.8);
	Color day_horizon_color = Color(0.68, 0.79, 0.9);
	Color dusk_top_color = Color(0.22, 0.27, 0.47);
	Color dusk_horizon_color = Color(0.96, 0.57, 0.36);
	Color night_top_color = Color(0.012, 0.016, 0.035);
	Color night_horizon_color = Color(0.035, 0.045, 0.075);
	Color ground_color = Color(0.32, 0.34, 0.37);
	Color overcast_color = Color(0.58, 0.6, 0.64);
	float cloud_cover = 0.4;
	float cloud_scale = 1.0;
	float energy_multiplier = 1.0;

	static Mutex shader_mutex;
	static RID shader;
	mutable bool shader_set = false;
	static void _update_shader();

protected:
	static void _bind_methods();

public:
	void set_day_top_color(const Color &p_color);
	Color get_day_top_color() const;
	void set_day_horizon_color(const Color &p_color);
	Color get_day_horizon_color() const;
	void set_dusk_top_color(const Color &p_color);
	Color get_dusk_top_color() const;
	void set_dusk_horizon_color(const Color &p_color);
	Color get_dusk_horizon_color() const;
	void set_night_top_color(const Color &p_color);
	Color get_night_top_color() const;
	void set_night_horizon_color(const Color &p_color);
	Color get_night_horizon_color() const;
	void set_ground_color(const Color &p_color);
	Color get_ground_color() const;
	void set_overcast_color(const Color &p_color);
	Color get_overcast_color() const;
	void set_cloud_cover(float p_cover);
	float get_cloud_cover() const;
	void set_cloud_scale(float p_scale);
	float get_cloud_scale() const;
	void set_energy_multiplier(float p_multiplier);
	float get_energy_multiplier() const;

	virtual Shader::Mode get_shader_mode() const override;
	virtual RID get_shader_rid() const override;
	virtual RID get_rid() const override;

	static void cleanup_shader();

	CloudySkyMaterial();
};
