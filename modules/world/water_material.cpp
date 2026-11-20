#include "water_material.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "modules/noise/fastnoise_lite.h"
#include "modules/noise/noise_texture_2d.h"
#include "servers/rendering/renderer_compositor.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/rendering_server_globals.h"

#include "modules/graphics/sky_globals.h"

Mutex WaterMaterial::shader_mutex;
RID WaterMaterial::shader;
Ref<Texture2D> WaterMaterial::ripples_a;
Ref<Texture2D> WaterMaterial::ripples_b;

namespace {
// The waves: each a Gerstner wave, a fraction of the longest's length and height, turned from the
// main direction by an angle (degrees). The same here and in the shader.
struct Wave {
	float length;
	float height;
	float angle;
};
const Wave WAVES[4] = { { 1.0, 1.0, 0.0 }, { 0.61, 0.55, 28.0 }, { 0.37, 0.3, -37.0 }, { 0.23, 0.18, 61.0 } };
constexpr float GRAVITY = 9.81;

// The shader's waves() is the same sum as get_height_at() below, with the same WAVES.
const char *WATER_SHADER = R"(
shader_type spatial;
render_mode cull_back, depth_draw_always, specular_schlick_ggx;

uniform vec3 water_color : source_color;
uniform vec3 deep_color : source_color;
uniform float clarity;
uniform float wave_height;
uniform float wave_length;
uniform vec2 wave_direction;
uniform float follow_wind;
uniform float choppiness;
uniform vec2 flow;
uniform float foam;
uniform float roughness_amount;
uniform float refraction;
uniform sampler2D ripples_a : hint_normal, filter_linear_mipmap, repeat_enable;
uniform sampler2D ripples_b : hint_normal, filter_linear_mipmap, repeat_enable;
uniform sampler2D screen_texture : hint_screen_texture, filter_linear, repeat_disable;
uniform sampler2D depth_texture : hint_depth_texture, filter_nearest, repeat_disable;

global uniform vec3 map_offset;
global uniform vec2 weather_wind;
global uniform float weather_rain;

varying vec3 v_map;
varying vec3 v_normal;
varying float v_crest;

// Where the waves go and how high: Weather3D's wind, unless the water has its own direction.
vec2 main_direction() {
	float speed = length(weather_wind);
	if (follow_wind > 0.5 && speed > 0.1) {
		return weather_wind / speed;
	}
	return length(wave_direction) > 0.0 ? normalize(wave_direction) : vec2(1.0, 0.0);
}

float strength() {
	return follow_wind > 0.5 ? clamp(length(weather_wind) / 6.0, 0.25, 2.0) : 1.0;
}

// The sum of the waves at a map position: how far it moves (xz along, y up), the surface's normal,
// and how sharp its crest is.
void waves(vec2 at, float time, out vec3 moved, out vec3 normal, out float crest) {
	vec3 SHAPES[4] = vec3[4](vec3(1.0, 1.0, 0.0), vec3(0.61, 0.55, 28.0), vec3(0.37, 0.3, -37.0), vec3(0.23, 0.18, 61.0));
	vec2 main = main_direction();
	float height = wave_height * strength();
	moved = vec3(0.0);
	vec3 n = vec3(0.0, 1.0, 0.0);
	crest = 0.0;
	for (int i = 0; i < 4; i++) {
		float turn = radians(SHAPES[i].z);
		vec2 d = vec2(main.x * cos(turn) - main.y * sin(turn), main.x * sin(turn) + main.y * cos(turn));
		float k = 6.2831853 / max(wave_length * SHAPES[i].x, 0.01);
		float a = height * SHAPES[i].y * 0.5;
		float q = clamp(choppiness / max(k * a * 4.0, 1e-4), 0.0, 1.0);
		float f = k * (dot(d, at) - sqrt(9.81 / k) * time);
		moved += vec3(d.x * q * a * cos(f), a * sin(f), d.y * q * a * cos(f));
		n.xz -= d * k * a * cos(f);
		n.y -= q * k * a * sin(f);
		crest += q * k * a * sin(f);
	}
	normal = normalize(n);
}

// Rings spreading where raindrops land, as a tilt of the surface.
float ripple_hash(vec2 p) {
	uvec2 q = uvec2(ivec2(floor(p)) + 32768);
	uint v = q.x * 747796405u ^ (q.y * 2891336453u);
	v = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
	return float((v >> 22u) ^ v) * (1.0 / 4294967296.0);
}

vec2 raindrops(vec2 at, float time) {
	vec2 tilt = vec2(0.0);
	vec2 p = at * 2.5;
	vec2 cell = floor(p);
	for (int y = -1; y <= 1; y++) {
		for (int x = -1; x <= 1; x++) {
			vec2 c = cell + vec2(float(x), float(y));
			vec2 centre = c + vec2(ripple_hash(c + 3.1), ripple_hash(c + 7.7));
			float age = fract(time * 1.3 + ripple_hash(c));
			vec2 away = p - centre;
			float d = length(away);
			float ring = d - age * 0.9;
			tilt += away / max(d, 1e-4) * sin(ring * 40.0) * exp(-ring * ring * 60.0) * (1.0 - age) * (1.0 - age);
		}
	}
	return tilt * 0.12;
}

void vertex() {
	vec3 world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_map = world + map_offset;
	vec3 moved;
	vec3 normal;
	float crest;
	waves(v_map.xz, TIME, moved, normal, crest);
	v_normal = normal;
	v_crest = crest;
	VERTEX += inverse(mat3(MODEL_MATRIX)) * moved;
	NORMAL = normalize(inverse(mat3(MODEL_MATRIX)) * v_normal);
	v_map += moved;
}

void fragment() {
	// The waves' normal, roughened by small ripples drifting with the wind and the flow, and by rain.
	vec2 main = main_direction();
	vec2 at = v_map.xz - flow * TIME;
	vec3 small_a = texture(ripples_a, at * 0.11 + main * TIME * 0.025).rgb * 2.0 - 1.0;
	vec3 small_b = texture(ripples_b, at * 0.07 - vec2(main.y, -main.x) * TIME * 0.018).rgb * 2.0 - 1.0;
	vec2 tilt = (small_a.xy + small_b.xy) * 0.25 * clamp(strength(), 0.4, 1.5);
	if (weather_rain > 0.01) {
		tilt += raindrops(v_map.xz, TIME) * weather_rain;
	}
	vec3 world_normal = normalize(v_normal + vec3(tilt.x, 0.0, tilt.y));
	NORMAL = normalize(mat3(VIEW_MATRIX) * world_normal);

	// What's under the surface, bent by the waves; anything in front of the water isn't.
	vec2 bent = SCREEN_UV + NORMAL.xz * refraction;
	float raw = texture(depth_texture, bent).r;
	vec4 under = INV_PROJECTION_MATRIX * vec4(bent * 2.0 - 1.0, raw, 1.0);
	under.xyz /= under.w;
	if (under.z > VERTEX.z) {
		bent = SCREEN_UV;
		raw = texture(depth_texture, bent).r;
		under = INV_PROJECTION_MATRIX * vec4(bent * 2.0 - 1.0, raw, 1.0);
		under.xyz /= under.w;
	}
	// How much water the light comes through: color is what's left after clarity metres.
	float through = max(VERTEX.z - under.z, 0.0);
	vec3 kept = pow(max(water_color, vec3(0.001)), vec3(through / max(clarity, 0.01)));
	vec3 below = texture(screen_texture, bent).rgb * kept;

	// Foam along shores, where the water is shallow, and on the steepest crests.
	float shore = 1.0 - smoothstep(0.0, 0.8, through);
	float pattern = texture(ripples_a, at * 0.35).r * texture(ripples_b, at * 0.21 + 0.3).r;
	float foamy = clamp((shore * 1.4 + smoothstep(0.55, 0.95, v_crest) * 0.8) * foam, 0.0, 1.0);
	foamy *= smoothstep(0.2, 0.45, pattern + foamy * 0.25);

	float facing = max(dot(NORMAL, VIEW), 0.0);
	float fresnel = 0.02 + 0.98 * pow(1.0 - facing, 5.0);
	// Deep water's own colour, lit by the scene; what's under it shows through, unlit again.
	ALBEDO = mix(deep_color * (1.0 - kept.g), vec3(0.9), foamy);
	EMISSION = below * (1.0 - fresnel) * (1.0 - foamy);
	ROUGHNESS = mix(roughness_amount, 0.7, foamy);
	SPECULAR = 0.5;
	// Fades out right at the shore, so the edge isn't a hard line.
	ALPHA = smoothstep(0.0, 0.08, through);
}
)";

Ref<Texture2D> make_ripples(int p_seed, float p_frequency) {
	Ref<FastNoiseLite> noise;
	noise.instantiate();
	noise->set_seed(p_seed);
	noise->set_noise_type(FastNoiseLite::TYPE_SIMPLEX_SMOOTH);
	noise->set_frequency(p_frequency);
	noise->set_fractal_octaves(4);
	Ref<NoiseTexture2D> texture;
	texture.instantiate();
	texture->set_width(256);
	texture->set_height(256);
	texture->set_seamless(true);
	texture->set_as_normal_map(true);
	texture->set_bump_strength(4.0);
	texture->set_generate_mipmaps(true);
	texture->set_noise(noise);
	return texture;
}
} // namespace

WaterMaterial::WaterMaterial() {
	_set_material(RS::get_singleton()->material_create());
	set_color(color);
	set_deep_color(deep_color);
	set_clarity(clarity);
	set_wave_height(wave_height);
	set_wave_length(wave_length);
	set_wave_direction(wave_direction);
	set_follow_wind(follow_wind);
	set_choppiness(choppiness);
	set_flow(flow);
	set_foam(foam);
	set_roughness(roughness);
	set_refraction(refraction);
}

void WaterMaterial::_update_shader() {
	MutexLock lock(shader_mutex);
	if (shader.is_null()) {
		SkyGlobals::ensure();
		ripples_a = make_ripples(1, 0.02);
		ripples_b = make_ripples(2, 0.035);
		shader = RS::get_singleton()->shader_create();
		RS::get_singleton()->shader_set_code(shader, WATER_SHADER);
	}
}

void WaterMaterial::cleanup_shader() {
	if (shader.is_valid()) {
		RS::get_singleton()->free_rid(shader);
		shader = RID();
	}
	ripples_a.unref();
	ripples_b.unref();
}

Shader::Mode WaterMaterial::get_shader_mode() const {
	return Shader::MODE_SPATIAL;
}

RID WaterMaterial::get_shader_rid() const {
	_update_shader();
	return shader;
}

RID WaterMaterial::get_rid() const {
	_update_shader();
	if (!shader_set) {
		RS::get_singleton()->material_set_shader(_get_material(), shader);
		RS::get_singleton()->material_set_param(_get_material(), "ripples_a", ripples_a->get_rid());
		RS::get_singleton()->material_set_param(_get_material(), "ripples_b", ripples_b->get_rid());
		shader_set = true;
	}
	return _get_material();
}

Vector2 WaterMaterial::_direction() const {
	const Vector2 wind = SkyGlobals::get(SkyGlobals::WIND);
	if (follow_wind && wind.length() > 0.1f) {
		return wind.normalized();
	}
	return wave_direction.length() > 0.0f ? wave_direction.normalized() : Vector2(1, 0);
}

float WaterMaterial::_strength() const {
	const Vector2 wind = SkyGlobals::get(SkyGlobals::WIND);
	return follow_wind ? CLAMP(wind.length() / 6.0f, 0.25f, 2.0f) : 1.0f;
}

float WaterMaterial::get_height_at(const Vector2 &p_map_position) const {
	// The shader's clock: the renderer's, wrapped as it wraps TIME.
	const double rollover = MAX(double(GLOBAL_GET("rendering/limits/time/time_rollover_secs")), 1.0);
	const float time = float(Math::fmod(RSG::rasterizer != nullptr ? RSG::rasterizer->get_total_time() : 0.0, rollover));
	const Vector2 main = _direction();
	const float height = wave_height * _strength();
	// The waves move the surface sideways too: the point that ends up here started a little off.
	Vector2 from = p_map_position;
	float y = 0.0f;
	for (int pass = 0; pass < 4; pass++) {
		Vector2 along;
		y = 0.0f;
		for (const Wave &wave : WAVES) {
			const Vector2 d = main.rotated(Math::deg_to_rad(wave.angle));
			const float k = Math::TAU / MAX(wave_length * wave.length, 0.01f);
			const float a = height * wave.height * 0.5f;
			const float q = CLAMP(choppiness / MAX(k * a * 4.0f, 1e-4f), 0.0f, 1.0f);
			const float f = k * (d.dot(from) - Math::sqrt(GRAVITY / k) * time);
			along += d * q * a * Math::cos(f);
			y += a * Math::sin(f);
		}
		from = p_map_position - along;
	}
	return y;
}

void WaterMaterial::set_color(const Color &p_color) {
	color = p_color;
	RS::get_singleton()->material_set_param(_get_material(), "water_color", color);
}

Color WaterMaterial::get_color() const {
	return color;
}

void WaterMaterial::set_deep_color(const Color &p_color) {
	deep_color = p_color;
	RS::get_singleton()->material_set_param(_get_material(), "deep_color", deep_color);
}

Color WaterMaterial::get_deep_color() const {
	return deep_color;
}

void WaterMaterial::set_clarity(float p_metres) {
	clarity = MAX(p_metres, 0.01f);
	RS::get_singleton()->material_set_param(_get_material(), "clarity", clarity);
}

float WaterMaterial::get_clarity() const {
	return clarity;
}

void WaterMaterial::set_wave_height(float p_metres) {
	wave_height = MAX(p_metres, 0.0f);
	RS::get_singleton()->material_set_param(_get_material(), "wave_height", wave_height);
}

float WaterMaterial::get_wave_height() const {
	return wave_height;
}

void WaterMaterial::set_wave_length(float p_metres) {
	wave_length = MAX(p_metres, 0.1f);
	RS::get_singleton()->material_set_param(_get_material(), "wave_length", wave_length);
}

float WaterMaterial::get_wave_length() const {
	return wave_length;
}

void WaterMaterial::set_wave_direction(const Vector2 &p_direction) {
	wave_direction = p_direction;
	RS::get_singleton()->material_set_param(_get_material(), "wave_direction", wave_direction);
}

Vector2 WaterMaterial::get_wave_direction() const {
	return wave_direction;
}

void WaterMaterial::set_follow_wind(bool p_follow) {
	follow_wind = p_follow;
	RS::get_singleton()->material_set_param(_get_material(), "follow_wind", follow_wind ? 1.0f : 0.0f);
}

bool WaterMaterial::is_following_wind() const {
	return follow_wind;
}

void WaterMaterial::set_choppiness(float p_choppiness) {
	choppiness = CLAMP(p_choppiness, 0.0f, 1.0f);
	RS::get_singleton()->material_set_param(_get_material(), "choppiness", choppiness);
}

float WaterMaterial::get_choppiness() const {
	return choppiness;
}

void WaterMaterial::set_flow(const Vector2 &p_flow) {
	flow = p_flow;
	RS::get_singleton()->material_set_param(_get_material(), "flow", flow);
}

Vector2 WaterMaterial::get_flow() const {
	return flow;
}

void WaterMaterial::set_foam(float p_foam) {
	foam = CLAMP(p_foam, 0.0f, 1.0f);
	RS::get_singleton()->material_set_param(_get_material(), "foam", foam);
}

float WaterMaterial::get_foam() const {
	return foam;
}

void WaterMaterial::set_roughness(float p_roughness) {
	roughness = CLAMP(p_roughness, 0.0f, 1.0f);
	RS::get_singleton()->material_set_param(_get_material(), "roughness_amount", roughness);
}

float WaterMaterial::get_roughness() const {
	return roughness;
}

void WaterMaterial::set_refraction(float p_refraction) {
	refraction = CLAMP(p_refraction, 0.0f, 0.2f);
	RS::get_singleton()->material_set_param(_get_material(), "refraction", refraction);
}

float WaterMaterial::get_refraction() const {
	return refraction;
}

void WaterMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_color", "color"), &WaterMaterial::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &WaterMaterial::get_color);
	ClassDB::bind_method(D_METHOD("set_deep_color", "color"), &WaterMaterial::set_deep_color);
	ClassDB::bind_method(D_METHOD("get_deep_color"), &WaterMaterial::get_deep_color);
	ClassDB::bind_method(D_METHOD("set_clarity", "metres"), &WaterMaterial::set_clarity);
	ClassDB::bind_method(D_METHOD("get_clarity"), &WaterMaterial::get_clarity);
	ClassDB::bind_method(D_METHOD("set_wave_height", "metres"), &WaterMaterial::set_wave_height);
	ClassDB::bind_method(D_METHOD("get_wave_height"), &WaterMaterial::get_wave_height);
	ClassDB::bind_method(D_METHOD("set_wave_length", "metres"), &WaterMaterial::set_wave_length);
	ClassDB::bind_method(D_METHOD("get_wave_length"), &WaterMaterial::get_wave_length);
	ClassDB::bind_method(D_METHOD("set_wave_direction", "direction"), &WaterMaterial::set_wave_direction);
	ClassDB::bind_method(D_METHOD("get_wave_direction"), &WaterMaterial::get_wave_direction);
	ClassDB::bind_method(D_METHOD("set_follow_wind", "follow"), &WaterMaterial::set_follow_wind);
	ClassDB::bind_method(D_METHOD("is_following_wind"), &WaterMaterial::is_following_wind);
	ClassDB::bind_method(D_METHOD("set_choppiness", "choppiness"), &WaterMaterial::set_choppiness);
	ClassDB::bind_method(D_METHOD("get_choppiness"), &WaterMaterial::get_choppiness);
	ClassDB::bind_method(D_METHOD("set_flow", "flow"), &WaterMaterial::set_flow);
	ClassDB::bind_method(D_METHOD("get_flow"), &WaterMaterial::get_flow);
	ClassDB::bind_method(D_METHOD("set_foam", "foam"), &WaterMaterial::set_foam);
	ClassDB::bind_method(D_METHOD("get_foam"), &WaterMaterial::get_foam);
	ClassDB::bind_method(D_METHOD("set_roughness", "roughness"), &WaterMaterial::set_roughness);
	ClassDB::bind_method(D_METHOD("get_roughness"), &WaterMaterial::get_roughness);
	ClassDB::bind_method(D_METHOD("set_refraction", "refraction"), &WaterMaterial::set_refraction);
	ClassDB::bind_method(D_METHOD("get_refraction"), &WaterMaterial::get_refraction);
	ClassDB::bind_method(D_METHOD("get_height_at", "map_position"), &WaterMaterial::get_height_at);

	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "deep_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_deep_color", "get_deep_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "clarity", PROPERTY_HINT_RANGE, "0.01,50,0.01,or_greater,suffix:m"), "set_clarity", "get_clarity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "foam", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_foam", "get_foam");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "roughness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_roughness", "get_roughness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "refraction", PROPERTY_HINT_RANGE, "0,0.2,0.001"), "set_refraction", "get_refraction");
	ADD_GROUP("Waves", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wave_height", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:m"), "set_wave_height", "get_wave_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wave_length", PROPERTY_HINT_RANGE, "0.1,200,0.1,or_greater,suffix:m"), "set_wave_length", "get_wave_length");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "choppiness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_choppiness", "get_choppiness");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "follow_wind"), "set_follow_wind", "is_following_wind");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "wave_direction"), "set_wave_direction", "get_wave_direction");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "flow", PROPERTY_HINT_NONE, "suffix:m/s"), "set_flow", "get_flow");
}
