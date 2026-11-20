#include "cloudy_sky_material.h"

#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

#include "modules/graphics/sky_clouds_shader.h"
#include "modules/graphics/sky_globals.h"
#include "modules/graphics/sky_night_shader.h"

Mutex CloudySkyMaterial::shader_mutex;
RID CloudySkyMaterial::shader;

namespace {
// The sky never reads TIME: a sky that does is drawn again into the reflections every frame. The
// clouds drift by a global (Weather3D's wind) instead.
const char *SKY_UNIFORMS = R"(
shader_type sky;
render_mode use_debanding;

uniform vec3 day_top_color : source_color;
uniform vec3 day_horizon_color : source_color;
uniform vec3 dusk_top_color : source_color;
uniform vec3 dusk_horizon_color : source_color;
uniform vec3 night_top_color : source_color;
uniform vec3 night_horizon_color : source_color;
uniform vec3 ground_color : source_color;
uniform vec3 overcast_color : source_color;
uniform float cloud_cover : hint_range(0.0, 1.0);
uniform float cloud_scale;
uniform float energy_multiplier;

global uniform vec3 time_of_day_sun_direction;
global uniform float weather_cloud_cover;
global uniform float weather_overcast;
global uniform vec2 weather_cloud_drift;
global uniform float weather_lightning;
)";

const char *SKY_FUNCTION = R"(
void sky() {
	// TimeOfDay's sun, even below the horizon; else the scene's first light.
	vec3 sun = length(time_of_day_sun_direction) > 0.5 ? normalize(time_of_day_sun_direction) : (LIGHT0_ENABLED ? LIGHT0_DIRECTION : vec3(0.0, 1.0, 0.0));
	float cover = weather_cloud_cover >= 0.0 ? weather_cloud_cover : cloud_cover;
	float overcast = max(weather_overcast, smoothstep(0.75, 1.0, cover));
	float day = smoothstep(-0.05, 0.3, sun.y);
	float night = 1.0 - smoothstep(-0.2, -0.02, sun.y);

	vec3 top = mix(mix(night_top_color, dusk_top_color, 1.0 - night), day_top_color, day);
	vec3 horizon = mix(mix(night_horizon_color, dusk_horizon_color, 1.0 - night), day_horizon_color, day);
	// Dusk's colour gathers on the sun's side of the sky.
	vec2 flat_eye = EYEDIR.xz / max(length(EYEDIR.xz), 1e-4);
	vec2 flat_sun = sun.xz / max(length(sun.xz), 1e-4);
	float towards_sun = pow(max(dot(flat_eye, flat_sun), 0.0), 2.0);
	horizon = mix(mix(night_horizon_color, day_horizon_color, day), horizon, mix(0.35, 1.0, towards_sun));
	float up = max(EYEDIR.y, 0.0);
	vec3 colour = mix(horizon, top, pow(up, mix(0.45, 0.3, night)));

	vec3 grey = overcast_color * mix(0.08, 1.0, 1.0 - night) * mix(0.7, 1.0, day);
	vec3 low_cloud = overcast_banks(EYEDIR, grey);
	colour = mix(colour, low_cloud, overcast);
	horizon = mix(horizon, grey, overcast);
	colour = night_sky(colour, EYEDIR, sun, night, 1.0 - overcast, dot(night_top_color, vec3(0.333)) * 1.5, 1.2);

	// The sun: a disc and a soft glow round it, hidden by the overcast.
	float facing = max(dot(EYEDIR, sun), 0.0);
	vec3 sun_colour = mix(vec3(1.0, 0.55, 0.3), vec3(1.0, 0.96, 0.9), day);
	colour += sun_colour * (pow(facing, 2000.0) * 40.0 + pow(facing, 12.0) * 0.18) * (1.0 - night) * (1.0 - overcast);

	if (EYEDIR.y > 0.0) {
		vec3 wisp_colour = mix(mix(night_horizon_color * 0.6, dusk_horizon_color, 1.0 - night), vec3(1.0), day);
		vec3 night_cloud = mix(night_top_color, night_horizon_color, 0.75) * 1.5;
		colour = painted_clouds(colour, EYEDIR, sun, night, day, cover, overcast, cloud_scale, wisp_colour, night_cloud, low_cloud);
	} else {
		// Below the horizon: the haze, as the fog has it.
		colour = mix(horizon, ground_color * mix(0.05, 1.0, 1.0 - night) * mix(0.6, 1.0, day), smoothstep(0.0, 0.08, -EYEDIR.y));
	}
	// A lightning flash lighting the clouds from inside.
	colour += vec3(0.75, 0.8, 1.0) * weather_lightning * 0.7 * smoothstep(-0.05, 0.4, EYEDIR.y);
	COLOR = colour * energy_multiplier;
}
)";
} // namespace

CloudySkyMaterial::CloudySkyMaterial() {
	_set_material(RS::get_singleton()->material_create());
	set_day_top_color(day_top_color);
	set_day_horizon_color(day_horizon_color);
	set_dusk_top_color(dusk_top_color);
	set_dusk_horizon_color(dusk_horizon_color);
	set_night_top_color(night_top_color);
	set_night_horizon_color(night_horizon_color);
	set_ground_color(ground_color);
	set_overcast_color(overcast_color);
	set_cloud_cover(cloud_cover);
	set_cloud_scale(cloud_scale);
	set_energy_multiplier(energy_multiplier);
}

void CloudySkyMaterial::_update_shader() {
	MutexLock lock(shader_mutex);
	if (shader.is_null()) {
		SkyGlobals::ensure();
		shader = RS::get_singleton()->shader_create();
		RS::get_singleton()->shader_set_code(shader, String(SKY_UNIFORMS) + SKY_NIGHT_SHADER_GLOBALS + SKY_CLOUDS_SHADER_CODE + SKY_NIGHT_SHADER_CODE + SKY_FUNCTION);
	}
}

void CloudySkyMaterial::cleanup_shader() {
	if (shader.is_valid()) {
		RS::get_singleton()->free_rid(shader);
		shader = RID();
	}
}

Shader::Mode CloudySkyMaterial::get_shader_mode() const {
	return Shader::MODE_SKY;
}

RID CloudySkyMaterial::get_shader_rid() const {
	_update_shader();
	return shader;
}

RID CloudySkyMaterial::get_rid() const {
	_update_shader();
	if (!shader_set) {
		RS::get_singleton()->material_set_shader(_get_material(), shader);
		shader_set = true;
	}
	return _get_material();
}

#define COLOR_PROPERTY(m_name)                                                                    \
	void CloudySkyMaterial::set_##m_name(const Color &p_color) {                                  \
		m_name = p_color;                                                                         \
		RS::get_singleton()->material_set_param(_get_material(), #m_name, m_name);              \
	}                                                                                             \
	Color CloudySkyMaterial::get_##m_name() const {                                               \
		return m_name;                                                                            \
	}

COLOR_PROPERTY(day_top_color)
COLOR_PROPERTY(day_horizon_color)
COLOR_PROPERTY(dusk_top_color)
COLOR_PROPERTY(dusk_horizon_color)
COLOR_PROPERTY(night_top_color)
COLOR_PROPERTY(night_horizon_color)
COLOR_PROPERTY(ground_color)
COLOR_PROPERTY(overcast_color)

void CloudySkyMaterial::set_cloud_cover(float p_cover) {
	cloud_cover = CLAMP(p_cover, 0.0f, 1.0f);
	RS::get_singleton()->material_set_param(_get_material(), "cloud_cover", cloud_cover);
}

float CloudySkyMaterial::get_cloud_cover() const {
	return cloud_cover;
}

void CloudySkyMaterial::set_cloud_scale(float p_scale) {
	cloud_scale = MAX(p_scale, 0.01f);
	RS::get_singleton()->material_set_param(_get_material(), "cloud_scale", cloud_scale);
}

float CloudySkyMaterial::get_cloud_scale() const {
	return cloud_scale;
}

void CloudySkyMaterial::set_energy_multiplier(float p_multiplier) {
	energy_multiplier = MAX(p_multiplier, 0.0f);
	RS::get_singleton()->material_set_param(_get_material(), "energy_multiplier", energy_multiplier);
}

float CloudySkyMaterial::get_energy_multiplier() const {
	return energy_multiplier;
}

void CloudySkyMaterial::_bind_methods() {
#define BIND_COLOR(m_name)                                                                                    \
	ClassDB::bind_method(D_METHOD("set_" #m_name, "color"), &CloudySkyMaterial::set_##m_name);                \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &CloudySkyMaterial::get_##m_name);                         \
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, #m_name, PROPERTY_HINT_COLOR_NO_ALPHA), "set_" #m_name, "get_" #m_name);

	ADD_GROUP("Day", "day_");
	BIND_COLOR(day_top_color)
	BIND_COLOR(day_horizon_color)
	ADD_GROUP("Dusk", "dusk_");
	BIND_COLOR(dusk_top_color)
	BIND_COLOR(dusk_horizon_color)
	ADD_GROUP("Night", "night_");
	BIND_COLOR(night_top_color)
	BIND_COLOR(night_horizon_color)
	ADD_GROUP("", "");
	BIND_COLOR(ground_color)
	ADD_GROUP("Clouds", "");
	BIND_COLOR(overcast_color)
#undef BIND_COLOR

	ClassDB::bind_method(D_METHOD("set_cloud_cover", "cover"), &CloudySkyMaterial::set_cloud_cover);
	ClassDB::bind_method(D_METHOD("get_cloud_cover"), &CloudySkyMaterial::get_cloud_cover);
	ClassDB::bind_method(D_METHOD("set_cloud_scale", "scale"), &CloudySkyMaterial::set_cloud_scale);
	ClassDB::bind_method(D_METHOD("get_cloud_scale"), &CloudySkyMaterial::get_cloud_scale);
	ClassDB::bind_method(D_METHOD("set_energy_multiplier", "multiplier"), &CloudySkyMaterial::set_energy_multiplier);
	ClassDB::bind_method(D_METHOD("get_energy_multiplier"), &CloudySkyMaterial::get_energy_multiplier);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cloud_cover", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_cloud_cover", "get_cloud_cover");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cloud_scale", PROPERTY_HINT_RANGE, "0.1,4,0.01"), "set_cloud_scale", "get_cloud_scale");
	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "energy_multiplier", PROPERTY_HINT_RANGE, "0,16,0.01"), "set_energy_multiplier", "get_energy_multiplier");
}
