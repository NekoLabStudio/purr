#pragma once

// AtmosphereSkyMaterial: the sky as the air makes it (Hillaire 2020): blue overhead, paler and
// hazier towards the horizon, reddening at dawn and dusk as the sun's light crosses more air, with
// the sun's disc dimmed and coloured by the air before it. Worked out on the GPU from the air's
// figures (AtmosphereTables) as the sun moves; TimeOfDay's sun, and Weather3D's clouds, through
// shader globals. AerialPerspectiveEffect adds the same air between the camera and the scene.

#include "core/os/mutex.h"
#include "scene/resources/material.h"

class AtmosphereSkyMaterial : public Material {
	GDCLASS(AtmosphereSkyMaterial, Material);

	float brightness = 9.0;
	float air_density = 1.0;
	float haze = 0.2;
	float haze_height = 600.0;
	Color ground_color = Color(0.3, 0.3, 0.3);
	float altitude = 40.0;
	Color night_glow = Color(0.004, 0.005, 0.009);
	Color overcast_color = Color(0.55, 0.57, 0.6);
	float cloud_cover = 0.4;
	float cloud_scale = 1.0;
	float sun_disc_size = 0.9;
	Vector3 sun_direction = Vector3(0.3, 0.8, 0.5);
	bool volumetric_clouds = true;
	Vector2 cloud_drift; // How far the wind has carried the volumetric clouds (km).
	uint64_t last_frame_usec = 0;

	int redrawn = 0;

	static Mutex shader_mutex;
	static RID shader;
	mutable bool shader_set = false;
	static void _update_shader();
	void _frame_pre_draw();
	Vector3 _sun() const;
	bool _in_use() const;
	void _step_clouds(const Vector3 &p_sun);

protected:
	static void _bind_methods();

public:
	void set_brightness(float p_brightness);
	float get_brightness() const;
	void set_air_density(float p_density);
	float get_air_density() const;
	void set_haze(float p_haze);
	float get_haze() const;
	void set_haze_height(float p_metres);
	float get_haze_height() const;
	void set_ground_color(const Color &p_color);
	Color get_ground_color() const;
	void set_altitude(float p_metres);
	float get_altitude() const;
	void set_night_glow(const Color &p_color);
	Color get_night_glow() const;
	void set_overcast_color(const Color &p_color);
	Color get_overcast_color() const;
	void set_cloud_cover(float p_cover);
	float get_cloud_cover() const;
	void set_cloud_scale(float p_scale);
	float get_cloud_scale() const;
	void set_sun_disc_size(float p_degrees);
	float get_sun_disc_size() const;
	void set_volumetric_clouds(bool p_enabled);
	bool is_volumetric_clouds() const;
	void set_sun_direction(const Vector3 &p_direction);
	Vector3 get_sun_direction() const;

	// The air's figures as AtmosphereTables takes them, for the sun as it is now.
	PackedFloat32Array get_figures() const;

	virtual Shader::Mode get_shader_mode() const override;
	virtual RID get_shader_rid() const override;
	virtual RID get_rid() const override;

	static void cleanup_shader();

	AtmosphereSkyMaterial();
	~AtmosphereSkyMaterial();
};
