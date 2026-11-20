#pragma once

// Weather3D: clouds, rain, snow, fog and storms that change gradually towards what the game asks
// for. Its cloud cover, rain, snow and fog ease towards their targets; the ground's wetness follows
// the rain, getting wet quickly and drying slowly, and snow settles and melts more slowly still.
// Rain and snow fall round the camera and stop at roofs, the clouds drift with the wind, a
// TimeOfDay's sun dims under overcast, the world's environment thickens its fog, and the skies,
// SurfaceWeatherEffect and the game's own shaders read all of it through shader globals.

#include "core/math/random_number_generator.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/environment.h"

class DirectionalLight3D;
class GPUParticles3D;
class GPUParticlesCollisionHeightField3D;

class Weather3D : public Node3D {
	GDCLASS(Weather3D, Node3D);

	float cloud_cover = 0.3;
	float rain = 0.0;
	Vector2 wind = Vector2(4.0, 1.5);
	float change_time = 30.0;
	float wetting_time = 25.0;
	float drying_time = 90.0;
	int rain_amount = 8000;
	float snow = 0.0;
	int snow_amount = 6000;
	float settling_time = 120.0;
	float melting_time = 300.0;
	float fog = 0.0;
	float lightning = 0.0;
	NodePath time_of_day;

	float current_cover = 0.3;
	float current_rain = 0.0;
	float current_snow = 0.0;
	float current_fog = 0.0;
	float overcast = 0.0;
	float wetness = 0.0;
	float snow_cover = 0.0;
	Vector2 drift;
	GPUParticles3D *drops = nullptr;
	GPUParticles3D *flakes = nullptr;
	GPUParticlesCollisionHeightField3D *shelter = nullptr;

	// A storm's lightning: when the next flash comes, and the one under way.
	Ref<RandomNumberGenerator> random;
	float next_flash = 0.0;
	float flash_age = -1.0;
	float flash_strength = 0.0;
	DirectionalLight3D *flash_light = nullptr;

	// The environment's own fog, which the weather adds to.
	Ref<Environment> environment;
	bool own_fog_enabled = false;
	float own_fog_density = 0.0;
	float own_volumetric_fog_density = 0.0;

	GPUParticles3D *_make_particles(Node *p_parent, const Vector3 &p_box, float p_lifetime, const Size2 &p_size, const Color &p_color, bool p_streak);
	void _make_weather();
	void _step(float p_delta);
	void _step_lightning(float p_delta);
	void _follow_camera();
	void _apply_fog();
	void _restore_fog();
	void _publish();
	void _restart_drops(const Vector3 &p_by);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_cloud_cover(float p_cover);
	float get_cloud_cover() const;
	void set_rain(float p_rain);
	float get_rain() const;
	void set_wind(const Vector2 &p_wind);
	Vector2 get_wind() const;
	void set_change_time(float p_seconds);
	float get_change_time() const;
	void set_wetting_time(float p_seconds);
	float get_wetting_time() const;
	void set_drying_time(float p_seconds);
	float get_drying_time() const;
	void set_rain_amount(int p_amount);
	int get_rain_amount() const;
	void set_snow(float p_snow);
	float get_snow() const;
	void set_snow_amount(int p_amount);
	int get_snow_amount() const;
	void set_settling_time(float p_seconds);
	float get_settling_time() const;
	void set_melting_time(float p_seconds);
	float get_melting_time() const;
	void set_fog(float p_fog);
	float get_fog() const;
	void set_lightning(float p_lightning);
	float get_lightning() const;
	void set_time_of_day(const NodePath &p_path);
	NodePath get_time_of_day() const;

	float get_current_cloud_cover() const;
	float get_current_rain() const;
	float get_current_snow() const;
	float get_current_fog() const;
	float get_overcast() const;
	float get_wetness() const;
	float get_snow_cover() const;
	void settle();
	void flash();

	Weather3D();
};
