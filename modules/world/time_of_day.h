#pragma once

// TimeOfDay: moves a sun and a moon across the sky as the hours pass, and dims the sky and
// ambient light at night. Its sun and moon are its first two DirectionalLight3D children unless
// set; its rotation sets where north is. In the editor, changing time previews that hour.

#include "scene/3d/light_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/environment.h"

class TimeOfDay : public Node3D {
	GDCLASS(TimeOfDay, Node3D);

	float time = 12.0; // Hours, 0 to 24.
	float day_length = 1200.0; // Real seconds a day takes; 0 stops the clock.
	float latitude = 45.0;
	float sun_energy = 1.0;
	float moon_energy = 0.15;
	float night_sky_energy = 0.1; // The sky and ambient light at night, as a part of the day's.
	NodePath sun_path;
	NodePath moon_path;

	// The environment's own energies, which night scales down.
	Ref<Environment> environment;
	float day_background_energy = 1.0;
	float day_ambient_energy = 1.0;

	DirectionalLight3D *_light(const NodePath &p_path, const DirectionalLight3D *p_other) const;
	Ref<Environment> _environment() const;
	void _apply();
	void _place_light(DirectionalLight3D *p_light, const Vector3 &p_towards, float p_energy, const Color &p_color);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_time(float p_time);
	float get_time() const;
	void set_day_length(float p_seconds);
	float get_day_length() const;
	void set_latitude(float p_degrees);
	float get_latitude() const;
	void set_sun_energy(float p_energy);
	float get_sun_energy() const;
	void set_moon_energy(float p_energy);
	float get_moon_energy() const;
	void set_night_sky_energy(float p_energy);
	float get_night_sky_energy() const;
	void set_sun(const NodePath &p_path);
	NodePath get_sun() const;
	void set_moon(const NodePath &p_path);
	NodePath get_moon() const;

	Vector3 get_sun_direction() const;
	float get_daylight() const;
	bool is_day() const;

	TimeOfDay();
};
