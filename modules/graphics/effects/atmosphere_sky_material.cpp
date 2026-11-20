#include "atmosphere_sky_material.h"

#include "core/object/callable_mp.h"
#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/sky.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/environment.h"
#include "servers/rendering/rendering_server.h"

#include "atmosphere_tables.h"
#include "modules/graphics/sky_clouds_shader.h"
#include "modules/graphics/sky_globals.h"
#include "modules/graphics/sky_night_shader.h"

Mutex AtmosphereSkyMaterial::shader_mutex;
RID AtmosphereSkyMaterial::shader;

namespace {
// The Earth's air (Hillaire's figures), in kilometres.
const Vector3 RAYLEIGH_SCATTERING(5.802e-3, 13.558e-3, 33.1e-3);
const float RAYLEIGH_SCALE_HEIGHT = 8.0;
const float CLEAN_AIR_MIE = 0.004; // The haze aloft that's always there.
const float MIE_ALBEDO = 0.9;
const float MIE_ANISOTROPY = 0.75;
const Vector3 OZONE_ABSORPTION(0.650e-3, 1.881e-3, 0.085e-3);
const float OZONE_HEIGHT = 25.0;
const float OZONE_HALF_WIDTH = 15.0;
const float GROUND_RADIUS = 6360.0;
const float TOP_RADIUS = 6460.0;
// The sky is drawn again for its reflections only once the sun has moved this far (radians).
const float SUN_MOVED = 0.002;

const char *SKY_UNIFORMS = R"(
shader_type sky;
render_mode use_debanding;

uniform sampler2D sky_view_table : filter_linear, repeat_disable;
uniform sampler2D transmittance_table : filter_linear, repeat_disable;
// The planet's radius, the top of the air's and the camera's distance from the planet's middle (km).
uniform vec3 radii = vec3(6360.0, 6460.0, 6360.04);
uniform float sun_disc_radius = 0.008;
uniform float brightness = 9.0;
uniform vec3 night_glow : source_color;
uniform vec3 overcast_color : source_color;
uniform float cloud_cover : hint_range(0.0, 1.0);
uniform float cloud_scale = 1.0;
// The volumetric clouds round the camera by direction (screen_clouds_march.glsl): rgb what they and the
// air before them add, a what still shows of the sky behind, while volumetric is 1.
uniform sampler2D cloud_dome : filter_linear, repeat_enable;
uniform float volumetric = 0.0;
uniform vec3 fallback_sun = vec3(0.3, 0.8, 0.5);
// Changed when the tables are made again, so the sky's reflections are drawn again with them.
uniform float redrawn = 0.0;

global uniform vec3 time_of_day_sun_direction;
global uniform float weather_cloud_cover;
global uniform float weather_overcast;
global uniform vec2 weather_cloud_drift;
global uniform float weather_lightning;
)";

const char *SKY_FUNCTION = R"(
vec2 to_texel_middles(vec2 uv, vec2 size) {
	return (uv * (size - 1.0) + 0.5) / size;
}

// As atmosphere_inc.glsl's sky_view_uv, above the horizon.
vec2 sky_view_uv(float view_zenith_cos, float light_view_cos) {
	float view_r = radii.z;
	float horizon = sqrt(max(view_r * view_r - radii.x * radii.x, 0.0));
	float beta = acos(clamp(horizon / max(view_r, 1e-6), -1.0, 1.0));
	float zenith_horizon = PI - beta;
	float view_zenith = min(acos(clamp(view_zenith_cos, -1.0, 1.0)), zenith_horizon);
	float coord = 1.0 - sqrt(max(1.0 - view_zenith / max(zenith_horizon, 1e-6), 0.0));
	return vec2(sqrt(clamp(-light_view_cos * 0.5 + 0.5, 0.0, 1.0)), coord * 0.5);
}

// As atmosphere_inc.glsl's transmittance_uv.
vec2 transmittance_uv(float r, float mu) {
	float top = radii.y;
	float bottom = radii.x;
	float h = sqrt(max(top * top - bottom * bottom, 0.0));
	float rho = sqrt(max(r * r - bottom * bottom, 0.0));
	float discriminant = r * r * (mu * mu - 1.0) + top * top;
	float d = max(0.0, -r * mu + sqrt(max(discriminant, 0.0)));
	float d_min = top - r;
	float d_max = rho + h;
	return vec2((d - d_min) / max(d_max - d_min, 1e-6), rho / max(h, 1e-6));
}

void sky() {
	vec3 sun = length(time_of_day_sun_direction) > 0.5 ? normalize(time_of_day_sun_direction) : normalize(fallback_sun);
	float cover = weather_cloud_cover >= 0.0 ? weather_cloud_cover : cloud_cover;
	float overcast = max(weather_overcast, smoothstep(0.75, 1.0, cover));
	float day = smoothstep(-0.05, 0.3, sun.y);
	float night = 1.0 - smoothstep(-0.2, -0.02, sun.y);
	vec3 eye = EYEDIR;
	// Below the horizon: the horizon's light, as the far air has it.
	vec3 looking = normalize(vec3(eye.x, max(eye.y, 0.0005), eye.z));
	vec2 flat_eye = looking.xz / max(length(looking.xz), 1e-4);
	vec2 flat_sun = sun.xz / max(length(sun.xz), 1e-4);
	// Looking straight up, or the sun overhead: every way round is the same.
	float light_view_cos = (length(looking.xz) > 1e-3 && length(sun.xz) > 1e-3) ? dot(flat_eye, flat_sun) : 1.0;
	vec2 uv = to_texel_middles(sky_view_uv(looking.y, light_view_cos), vec2(textureSize(sky_view_table, 0)));
	vec3 colour = textureLod(sky_view_table, uv, 0.0).rgb;
	vec3 horizon = textureLod(sky_view_table, vec2(uv.x, 0.5), 0.0).rgb;

	// A night sky's glow, reaching further up the sky than daylight's haze.
	float up = max(eye.y, 0.0);
	colour += night_glow * mix(1.0, 0.4, pow(up, 0.3)) * night;
	horizon += night_glow * night;

	vec3 grey = overcast_color * brightness * 0.08 * mix(0.02, 1.0, 1.0 - night) * mix(0.6, 1.0, day);
	vec3 low_cloud = overcast_banks(eye, grey);
	// Under the volumetric clouds the overcast's even grey stays behind them, where they end short of the horizon.
	colour = mix(colour, volumetric > 0.5 ? grey : low_cloud, overcast * smoothstep(0.0, 0.12, eye.y));
	colour = night_sky(colour, eye, sun, night, 1.0 - overcast, dot(night_glow, vec3(0.333)) * 1.5, brightness * 0.12);

	// The sun's disc, through the air between it and the camera; soft at its edge.
	float apart = acos(clamp(dot(eye, sun), -1.0, 1.0));
	float disc = 1.0 - smoothstep(sun_disc_radius * 0.7, sun_disc_radius, apart);
	if (disc > 0.0) {
		vec3 through = textureLod(transmittance_table, to_texel_middles(transmittance_uv(radii.z, sun.y), vec2(textureSize(transmittance_table, 0))), 0.0).rgb;
		vec3 sun_light = through * 2000.0 * brightness;
		// A real sun outshines the sky round it even at a hazy dusk; unbounded at noon it floods the glow.
		sun_light *= min(1.0, 60.0 / max(max(sun_light.r, max(sun_light.g, sun_light.b)), 1e-6));
		colour += sun_light * disc * (1.0 - overcast) * step(0.0, sun.y + 0.01);
	}

	if (volumetric > 0.5) {
		if (eye.y > 0.0) {
			vec2 around = vec2(fract(atan(eye.z, eye.x) / (2.0 * PI)), 1.0 - sqrt(eye.y));
			vec4 cloud = textureLod(cloud_dome, around, 0.0);
			colour = colour * cloud.a + cloud.rgb;
		}
	} else {
		vec3 wisp_colour = mix(horizon * 1.2, vec3(brightness * 0.12), day);
		vec3 night_cloud = night_glow * 1.5;
		colour = painted_clouds(colour, eye, sun, night, day, cover, overcast, cloud_scale, wisp_colour, night_cloud, low_cloud);
	}
	if (eye.y < 0.0) {
		colour = horizon;
	}
	// A lightning flash lighting the clouds from inside.
	colour += vec3(0.75, 0.8, 1.0) * weather_lightning * brightness * 0.06 * smoothstep(-0.05, 0.4, eye.y);
	COLOR = colour * (1.0 + redrawn * 0.0);
}
)";
} // namespace

AtmosphereSkyMaterial::AtmosphereSkyMaterial() {
	_set_material(RS::get_singleton()->material_create());
	set_brightness(brightness);
	set_night_glow(night_glow);
	set_overcast_color(overcast_color);
	set_cloud_cover(cloud_cover);
	set_cloud_scale(cloud_scale);
	set_sun_disc_size(sun_disc_size);
	set_sun_direction(sun_direction);
	set_altitude(altitude);
	AtmosphereTables *tables = AtmosphereTables::get_singleton();
	RS::get_singleton()->material_set_param(_get_material(), "sky_view_table", tables->get_sky_view_texture()->get_rid());
	RS::get_singleton()->material_set_param(_get_material(), "transmittance_table", tables->get_transmittance_texture()->get_rid());
	RS::get_singleton()->material_set_param(_get_material(), "cloud_dome", tables->get_dome_texture()->get_rid());
	set_volumetric_clouds(volumetric_clouds);
	RS::get_singleton()->connect(SNAME("frame_pre_draw"), callable_mp(this, &AtmosphereSkyMaterial::_frame_pre_draw));
}

AtmosphereSkyMaterial::~AtmosphereSkyMaterial() {
	if (RS::get_singleton() != nullptr) {
		RS::get_singleton()->disconnect(SNAME("frame_pre_draw"), callable_mp(this, &AtmosphereSkyMaterial::_frame_pre_draw));
	}
}

void AtmosphereSkyMaterial::_update_shader() {
	MutexLock lock(shader_mutex);
	if (shader.is_null()) {
		SkyGlobals::ensure();
		shader = RS::get_singleton()->shader_create();
		RS::get_singleton()->shader_set_code(shader, String(SKY_UNIFORMS) + SKY_NIGHT_SHADER_GLOBALS + SKY_CLOUDS_SHADER_CODE + SKY_NIGHT_SHADER_CODE + SKY_FUNCTION);
	}
}

void AtmosphereSkyMaterial::cleanup_shader() {
	if (shader.is_valid()) {
		RS::get_singleton()->free_rid(shader);
		shader = RID();
	}
}

Shader::Mode AtmosphereSkyMaterial::get_shader_mode() const {
	return Shader::MODE_SKY;
}

RID AtmosphereSkyMaterial::get_shader_rid() const {
	_update_shader();
	return shader;
}

RID AtmosphereSkyMaterial::get_rid() const {
	_update_shader();
	if (!shader_set) {
		RS::get_singleton()->material_set_shader(_get_material(), shader);
		shader_set = true;
	}
	return _get_material();
}

// TimeOfDay's sun, even below the horizon; else sun_direction.
Vector3 AtmosphereSkyMaterial::_sun() const {
	SkyGlobals::ensure();
	const Vector3 global = SkyGlobals::get(SkyGlobals::SUN_DIRECTION);
	return global.length() > 0.5 ? global.normalized() : sun_direction.normalized();
}

PackedFloat32Array AtmosphereSkyMaterial::get_figures() const {
	const Vector3 sun = _sun();
	const float night = 1.0f - Math::smoothstep(-0.2f, -0.02f, sun.y);
	const float day = Math::smoothstep(-0.05f, 0.3f, sun.y);
	const float overcast = MAX(0.0f, float(SkyGlobals::get(SkyGlobals::OVERCAST)));
	const Vector3 rayleigh = RAYLEIGH_SCATTERING * air_density;
	const Vector3 ozone = OZONE_ABSORPTION * air_density;
	const float mie_extinction = haze + CLEAN_AIR_MIE;
	// The even light on the air: a night sky's glow, and under cloud the cloud's grey.
	const Color ambient = night_glow * night + overcast_color * (overcast * brightness * 0.04f * Math::lerp(0.05f, 1.0f, day));
	return PackedFloat32Array({
			rayleigh.x, rayleigh.y, rayleigh.z, RAYLEIGH_SCALE_HEIGHT,
			mie_extinction * MIE_ALBEDO, mie_extinction, MAX(haze_height / 1000.0f, 0.01f), MIE_ANISOTROPY,
			ozone.x, ozone.y, ozone.z, OZONE_HEIGHT,
			GROUND_RADIUS, TOP_RADIUS, MAX(altitude / 1000.0f, 0.001f), OZONE_HALF_WIDTH,
			sun.x, sun.y, sun.z, brightness * (1.0f - overcast * 0.8f),
			ambient.r, ambient.g, ambient.b, 0.0f,
			ground_color.r, ground_color.g, ground_color.b, 0.0f });
}

// The air's tables are the game's one set: in a game, the sky the main camera sees sets them, not
// every AtmosphereSkyMaterial that exists. The editor's previews may each set them.
bool AtmosphereSkyMaterial::_in_use() const {
	if (Engine::get_singleton()->is_editor_hint()) {
		return true;
	}
	SceneTree *tree = SceneTree::get_singleton();
	Viewport *root = tree != nullptr ? tree->get_root() : nullptr;
	if (root == nullptr) {
		return false;
	}
	const Camera3D *camera = root->get_camera_3d();
	Ref<Environment> environment = camera != nullptr ? camera->get_environment() : Ref<Environment>();
	if (environment.is_null() && root->find_world_3d().is_valid()) {
		environment = root->find_world_3d()->get_environment();
		if (environment.is_null()) {
			environment = root->find_world_3d()->get_fallback_environment();
		}
	}
	return environment.is_valid() && environment->get_sky().is_valid() && environment->get_sky()->get_material().ptr() == this;
}

// Each frame: the tables made again if the sun has moved enough to show, or the air has changed.
void AtmosphereSkyMaterial::_frame_pre_draw() {
	if (!_in_use()) {
		return;
	}
	AtmosphereTables *tables = AtmosphereTables::get_singleton();
	PackedFloat32Array figures = get_figures();
	const PackedFloat32Array &last = tables->get_figures();
	if (last.size() == figures.size()) {
		const Vector3 before(last[AtmosphereTables::SUN_FIGURES], last[AtmosphereTables::SUN_FIGURES + 1], last[AtmosphereTables::SUN_FIGURES + 2]);
		const Vector3 now(figures[AtmosphereTables::SUN_FIGURES], figures[AtmosphereTables::SUN_FIGURES + 1], figures[AtmosphereTables::SUN_FIGURES + 2]);
		if (before.angle_to(now) < SUN_MOVED) {
			for (int i = 0; i < 3; i++) {
				figures.set(AtmosphereTables::SUN_FIGURES + i, last[AtmosphereTables::SUN_FIGURES + i]);
			}
		}
	}
	const bool changed = tables->set_figures(figures);
	if (volumetric_clouds) {
		_step_clouds(_sun());
	}
	// The clouds change every frame as they drift: the reflections are drawn again with the air only.
	if (changed) {
		redrawn = (redrawn + 1) % 1024;
		RS::get_singleton()->material_set_param(_get_material(), "redrawn", float(redrawn));
	}
}

// The clouds round the main camera, as the weather has them, the next sixteenth of their dome.
void AtmosphereSkyMaterial::_step_clouds(const Vector3 &p_sun) {
	SceneTree *tree = SceneTree::get_singleton();
	const Camera3D *camera = tree != nullptr && tree->get_root() != nullptr ? tree->get_root()->get_camera_3d() : nullptr;
	if (camera == nullptr || Engine::get_singleton()->is_editor_hint()) {
		return; // The editor shows the painted clouds.
	}
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	const float delta = last_frame_usec == 0 ? 0.0f : MIN((now - last_frame_usec) / 1000000.0f, 0.25f);
	last_frame_usec = now;
	const Vector2 wind = SkyGlobals::get(SkyGlobals::WIND);
	cloud_drift += wind * (delta / 1000.0f);
	// Where the camera is on the map (WorldOrigin's offset added), in kilometres.
	Vector3 at = camera->get_global_position();
	if (Object *origin = Engine::get_singleton()->get_singleton_object("WorldOrigin")) {
		at += Vector3(origin->get(SNAME("offset")));
	}
	at /= 1000.0f;
	const float covered = float(SkyGlobals::get(SkyGlobals::CLOUD_COVER));
	const float overcast = CLAMP(float(SkyGlobals::get(SkyGlobals::OVERCAST)), 0.0f, 1.0f);
	const float rain = CLAMP(float(SkyGlobals::get(SkyGlobals::RAIN)), 0.0f, 1.0f);
	const float cover = covered >= 0.0f ? covered : cloud_cover;
	const float night = 1.0f - Math::smoothstep(-0.2f, -0.02f, p_sun.y);
	const Color glow = night_glow * 10.0f;
	// A layer of cumulus from 1.4 to 3.6 km, lowering to one grey deck from 0.7 to 2.2 km as it turns
	// overcast; a ray goes no further than 60 km; the shape's noise tiles every 9 km, the detail's 1.2 km.
	const PackedFloat32Array figures({
			at.x, MAX(altitude / 1000.0f, 0.001f), at.z, 0.0f,
			Math::lerp(cover, 1.0f, overcast), overcast, rain, 1.0f,
			cloud_drift.x, cloud_drift.y, 9.0f / cloud_scale, 1.2f / cloud_scale,
			Math::lerp(1.4f, 0.7f, overcast), Math::lerp(3.6f, 2.2f, overcast), 60.0f, night,
			glow.r, glow.g, glow.b, 1.0f });
	AtmosphereTables::get_singleton()->step_clouds(figures);
}

void AtmosphereSkyMaterial::set_volumetric_clouds(bool p_enabled) {
	volumetric_clouds = p_enabled;
	// The editor shows the painted clouds: the volumetric ones follow the game's camera.
	RS::get_singleton()->material_set_param(_get_material(), "volumetric", volumetric_clouds && !Engine::get_singleton()->is_editor_hint() ? 1.0f : 0.0f);
}

bool AtmosphereSkyMaterial::is_volumetric_clouds() const {
	return volumetric_clouds;
}

#define FLOAT_PROPERTY(m_name, m_clamp, m_param)                                                \
	void AtmosphereSkyMaterial::set_##m_name(float p_value) {                                   \
		m_name = m_clamp;                                                                       \
		if (m_param[0] != '\0') {                                                               \
			RS::get_singleton()->material_set_param(_get_material(), m_param, m_name);          \
		}                                                                                       \
	}                                                                                           \
	float AtmosphereSkyMaterial::get_##m_name() const {                                         \
		return m_name;                                                                          \
	}

FLOAT_PROPERTY(brightness, MAX(p_value, 0.0f), "brightness")
FLOAT_PROPERTY(air_density, MAX(p_value, 0.0f), "")
FLOAT_PROPERTY(haze, MAX(p_value, 0.0f), "")
FLOAT_PROPERTY(haze_height, MAX(p_value, 10.0f), "")
FLOAT_PROPERTY(cloud_cover, CLAMP(p_value, 0.0f, 1.0f), "cloud_cover")
FLOAT_PROPERTY(cloud_scale, MAX(p_value, 0.01f), "cloud_scale")

void AtmosphereSkyMaterial::set_altitude(float p_metres) {
	altitude = MAX(p_metres, 1.0f);
	RS::get_singleton()->material_set_param(_get_material(), "radii", Vector3(GROUND_RADIUS, TOP_RADIUS, GROUND_RADIUS + altitude / 1000.0f));
}

float AtmosphereSkyMaterial::get_altitude() const {
	return altitude;
}

void AtmosphereSkyMaterial::set_sun_disc_size(float p_degrees) {
	sun_disc_size = CLAMP(p_degrees, 0.0f, 20.0f);
	RS::get_singleton()->material_set_param(_get_material(), "sun_disc_radius", Math::deg_to_rad(sun_disc_size) * 0.5f);
}

float AtmosphereSkyMaterial::get_sun_disc_size() const {
	return sun_disc_size;
}

void AtmosphereSkyMaterial::set_ground_color(const Color &p_color) {
	ground_color = p_color;
}

Color AtmosphereSkyMaterial::get_ground_color() const {
	return ground_color;
}

void AtmosphereSkyMaterial::set_night_glow(const Color &p_color) {
	night_glow = p_color;
	RS::get_singleton()->material_set_param(_get_material(), "night_glow", night_glow);
}

Color AtmosphereSkyMaterial::get_night_glow() const {
	return night_glow;
}

void AtmosphereSkyMaterial::set_overcast_color(const Color &p_color) {
	overcast_color = p_color;
	RS::get_singleton()->material_set_param(_get_material(), "overcast_color", overcast_color);
}

Color AtmosphereSkyMaterial::get_overcast_color() const {
	return overcast_color;
}

void AtmosphereSkyMaterial::set_sun_direction(const Vector3 &p_direction) {
	sun_direction = p_direction.length() > 0.0 ? p_direction.normalized() : Vector3(0, 1, 0);
	RS::get_singleton()->material_set_param(_get_material(), "fallback_sun", sun_direction);
}

Vector3 AtmosphereSkyMaterial::get_sun_direction() const {
	return sun_direction;
}

void AtmosphereSkyMaterial::_bind_methods() {
#define BIND(m_name, m_type, m_hint, m_hint_string)                                                                  \
	ClassDB::bind_method(D_METHOD("set_" #m_name, #m_name), &AtmosphereSkyMaterial::set_##m_name);                  \
	ClassDB::bind_method(D_METHOD("get_" #m_name), &AtmosphereSkyMaterial::get_##m_name);                           \
	ADD_PROPERTY(PropertyInfo(Variant::m_type, #m_name, m_hint, m_hint_string), "set_" #m_name, "get_" #m_name);

	BIND(brightness, FLOAT, PROPERTY_HINT_RANGE, "0,64,0.01,or_greater")
	BIND(sun_disc_size, FLOAT, PROPERTY_HINT_RANGE, "0,10,0.01,degrees")
	BIND(sun_direction, VECTOR3, PROPERTY_HINT_NONE, "")
	ADD_GROUP("Air", "");
	BIND(air_density, FLOAT, PROPERTY_HINT_RANGE, "0,4,0.01,or_greater")
	BIND(haze, FLOAT, PROPERTY_HINT_RANGE, "0,4,0.001,or_greater")
	BIND(haze_height, FLOAT, PROPERTY_HINT_RANGE, "10,8000,1,or_greater,suffix:m")
	BIND(ground_color, COLOR, PROPERTY_HINT_COLOR_NO_ALPHA, "")
	BIND(altitude, FLOAT, PROPERTY_HINT_RANGE, "1,10000,1,or_greater,suffix:m")
	BIND(night_glow, COLOR, PROPERTY_HINT_COLOR_NO_ALPHA, "")
	ADD_GROUP("Clouds", "");
	ClassDB::bind_method(D_METHOD("set_volumetric_clouds", "enabled"), &AtmosphereSkyMaterial::set_volumetric_clouds);
	ClassDB::bind_method(D_METHOD("is_volumetric_clouds"), &AtmosphereSkyMaterial::is_volumetric_clouds);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "volumetric_clouds"), "set_volumetric_clouds", "is_volumetric_clouds");
	BIND(overcast_color, COLOR, PROPERTY_HINT_COLOR_NO_ALPHA, "")
	BIND(cloud_cover, FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01")
	BIND(cloud_scale, FLOAT, PROPERTY_HINT_RANGE, "0.1,4,0.01")
#undef BIND
}
