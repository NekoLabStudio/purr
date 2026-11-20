#include "foliage_material.h"

#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

#include "modules/graphics/sky_globals.h"
#include "modules/graphics/weather_snow_shader.h"

#include "foliage_bender_3d.h"

Mutex FoliageMaterial::shader_mutex;
RID FoliageMaterial::shader;

namespace {
const char *FOLIAGE_SHADER = R"(
shader_type spatial;
render_mode cull_disabled;

uniform vec4 albedo_color : source_color;
uniform sampler2D albedo_texture : source_color, hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float alpha_scissor_threshold : hint_range(0.0, 1.0);
uniform sampler2D normal_texture : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float normal_strength;
uniform float roughness : hint_range(0.0, 1.0);
uniform vec3 backlight : source_color;
uniform float sway;
uniform float sway_height;
uniform float stiffness;

global uniform vec2 weather_wind;
global uniform sampler2D foliage_benders;
//WEATHER_SNOW

void vertex() {
	vec3 origin = MODEL_MATRIX[3].xyz;
	float speed = length(weather_wind);
	vec2 along = speed > 0.001 ? weather_wind / speed : vec2(1.0, 0.0);
	// The higher up the plant, the further it bends: nothing at the root.
	float up = clamp(VERTEX.y / max(sway_height, 0.01), 0.0, 1.0);
	float bend = up * up;
	// A steady lean with the wind, gusts that roll across the world with it, and a flutter of
	// each copy's own, quicker for stiffer plants.
	float phase = dot(origin.xz, along) * 0.15 - TIME * (0.6 + speed * 0.12);
	float gust = 0.5 + 0.5 * sin(phase) * sin(phase * 0.37 + 1.3);
	float flutter = sin(TIME * 2.3 * stiffness + origin.x * 1.7 + origin.z * 2.1 + VERTEX.x * 3.0 + VERTEX.z * 3.0) * 0.15;
	float amount = sway * bend * min(speed, 25.0) * 0.02 * (0.4 + gust + flutter) / max(stiffness, 0.05);
	vec3 push = inverse(mat3(MODEL_MATRIX)) * vec3(along.x, 0.0, along.y) * amount;
	VERTEX += push;
	VERTEX.y -= length(push) * 0.3 * up; // Bending over, it's a little lower.
	// Pushed aside and pressed down by FoliageBender3Ds nearby: players, animals, wheels.
	vec3 aside = vec3(0.0);
	for (int i = 0; i < 16; i++) {
		vec4 bender = texelFetch(foliage_benders, ivec2(i * 2, 0), 0);
		if (bender.w <= 0.0) {
			break;
		}
		vec2 away = origin.xz - bender.xz;
		float apart = length(away);
		// Only plants growing about where the bender stands, not those on a floor below or above.
		float level = step(bender.y - 1.5, origin.y) * step(origin.y, bender.y + 1.0);
		float near = (1.0 - smoothstep(bender.w * 0.35, bender.w, apart)) * level * texelFetch(foliage_benders, ivec2(i * 2 + 1, 0), 0).x;
		aside.xz += (apart > 0.01 ? away / apart : vec2(0.0)) * near;
		aside.y -= near;
	}
	float pressed = min(length(aside), 1.0);
	aside = vec3(aside.x * sway_height * 0.7 * bend, -pressed * sway_height * 0.45 * up, aside.z * sway_height * 0.7 * bend);
	VERTEX += inverse(mat3(MODEL_MATRIX)) * aside;
}

void fragment() {
	vec4 albedo = albedo_color * texture(albedo_texture, UV);
	// Weather3D's snow, lying on whatever faces up under open sky, from either side of a leaf.
	vec3 world_normal = normalize(mat3(INV_VIEW_MATRIX) * NORMAL);
	world_normal.y = abs(world_normal.y);
	float snow = weather_snow((INV_VIEW_MATRIX * vec4(VERTEX, 1.0)).xyz, world_normal);
	albedo.rgb = mix(albedo.rgb, vec3(0.9, 0.92, 0.95), snow * 0.85);
	ALBEDO = albedo.rgb;
	ALPHA = albedo.a;
	ALPHA_SCISSOR_THRESHOLD = alpha_scissor_threshold;
	ROUGHNESS = roughness;
	NORMAL_MAP = texture(normal_texture, UV).rgb;
	NORMAL_MAP_DEPTH = normal_strength;
	BACKLIGHT = backlight;
}
)";
} // namespace

FoliageMaterial::FoliageMaterial() {
	_set_material(RS::get_singleton()->material_create());
	set_albedo_color(albedo_color);
	set_alpha_scissor_threshold(alpha_scissor_threshold);
	set_normal_strength(normal_strength);
	set_roughness(roughness);
	set_backlight(backlight);
	set_sway(sway);
	set_sway_height(sway_height);
	set_stiffness(stiffness);
}

void FoliageMaterial::_update_shader() {
	MutexLock lock(shader_mutex);
	if (shader.is_null()) {
		SkyGlobals::ensure();
		FoliageBender3D::ensure_global();
		shader = RS::get_singleton()->shader_create();
		RS::get_singleton()->shader_set_code(shader, String(FOLIAGE_SHADER).replace("//WEATHER_SNOW", WEATHER_SNOW_DECLARATIONS));
	}
}

void FoliageMaterial::cleanup_shader() {
	if (shader.is_valid()) {
		RS::get_singleton()->free_rid(shader);
		shader = RID();
	}
}

Shader::Mode FoliageMaterial::get_shader_mode() const {
	return Shader::MODE_SPATIAL;
}

RID FoliageMaterial::get_shader_rid() const {
	_update_shader();
	return shader;
}

RID FoliageMaterial::get_rid() const {
	_update_shader();
	if (!shader_set) {
		RS::get_singleton()->material_set_shader(_get_material(), shader);
		shader_set = true;
	}
	return _get_material();
}

void FoliageMaterial::_set_texture(const StringName &p_param, const Ref<Texture2D> &p_texture) {
	RS::get_singleton()->material_set_param(_get_material(), p_param, p_texture.is_valid() ? Variant(p_texture->get_rid()) : Variant());
}

void FoliageMaterial::set_albedo_color(const Color &p_color) {
	albedo_color = p_color;
	RS::get_singleton()->material_set_param(_get_material(), "albedo_color", albedo_color);
}

Color FoliageMaterial::get_albedo_color() const {
	return albedo_color;
}

void FoliageMaterial::set_albedo_texture(const Ref<Texture2D> &p_texture) {
	albedo_texture = p_texture;
	_set_texture("albedo_texture", albedo_texture);
}

Ref<Texture2D> FoliageMaterial::get_albedo_texture() const {
	return albedo_texture;
}

void FoliageMaterial::set_alpha_scissor_threshold(float p_threshold) {
	alpha_scissor_threshold = CLAMP(p_threshold, 0.0f, 1.0f);
	RS::get_singleton()->material_set_param(_get_material(), "alpha_scissor_threshold", alpha_scissor_threshold);
}

float FoliageMaterial::get_alpha_scissor_threshold() const {
	return alpha_scissor_threshold;
}

void FoliageMaterial::set_normal_texture(const Ref<Texture2D> &p_texture) {
	normal_texture = p_texture;
	_set_texture("normal_texture", normal_texture);
}

Ref<Texture2D> FoliageMaterial::get_normal_texture() const {
	return normal_texture;
}

void FoliageMaterial::set_normal_strength(float p_strength) {
	normal_strength = p_strength;
	RS::get_singleton()->material_set_param(_get_material(), "normal_strength", normal_strength);
}

float FoliageMaterial::get_normal_strength() const {
	return normal_strength;
}

void FoliageMaterial::set_roughness(float p_roughness) {
	roughness = CLAMP(p_roughness, 0.0f, 1.0f);
	RS::get_singleton()->material_set_param(_get_material(), "roughness", roughness);
}

float FoliageMaterial::get_roughness() const {
	return roughness;
}

void FoliageMaterial::set_backlight(const Color &p_color) {
	backlight = p_color;
	RS::get_singleton()->material_set_param(_get_material(), "backlight", backlight);
}

Color FoliageMaterial::get_backlight() const {
	return backlight;
}

void FoliageMaterial::set_sway(float p_sway) {
	sway = MAX(p_sway, 0.0f);
	RS::get_singleton()->material_set_param(_get_material(), "sway", sway);
}

float FoliageMaterial::get_sway() const {
	return sway;
}

void FoliageMaterial::set_sway_height(float p_height) {
	sway_height = MAX(p_height, 0.01f);
	RS::get_singleton()->material_set_param(_get_material(), "sway_height", sway_height);
}

float FoliageMaterial::get_sway_height() const {
	return sway_height;
}

void FoliageMaterial::set_stiffness(float p_stiffness) {
	stiffness = MAX(p_stiffness, 0.05f);
	RS::get_singleton()->material_set_param(_get_material(), "stiffness", stiffness);
}

float FoliageMaterial::get_stiffness() const {
	return stiffness;
}

void FoliageMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_albedo_color", "color"), &FoliageMaterial::set_albedo_color);
	ClassDB::bind_method(D_METHOD("get_albedo_color"), &FoliageMaterial::get_albedo_color);
	ClassDB::bind_method(D_METHOD("set_albedo_texture", "texture"), &FoliageMaterial::set_albedo_texture);
	ClassDB::bind_method(D_METHOD("get_albedo_texture"), &FoliageMaterial::get_albedo_texture);
	ClassDB::bind_method(D_METHOD("set_alpha_scissor_threshold", "threshold"), &FoliageMaterial::set_alpha_scissor_threshold);
	ClassDB::bind_method(D_METHOD("get_alpha_scissor_threshold"), &FoliageMaterial::get_alpha_scissor_threshold);
	ClassDB::bind_method(D_METHOD("set_normal_texture", "texture"), &FoliageMaterial::set_normal_texture);
	ClassDB::bind_method(D_METHOD("get_normal_texture"), &FoliageMaterial::get_normal_texture);
	ClassDB::bind_method(D_METHOD("set_normal_strength", "strength"), &FoliageMaterial::set_normal_strength);
	ClassDB::bind_method(D_METHOD("get_normal_strength"), &FoliageMaterial::get_normal_strength);
	ClassDB::bind_method(D_METHOD("set_roughness", "roughness"), &FoliageMaterial::set_roughness);
	ClassDB::bind_method(D_METHOD("get_roughness"), &FoliageMaterial::get_roughness);
	ClassDB::bind_method(D_METHOD("set_backlight", "color"), &FoliageMaterial::set_backlight);
	ClassDB::bind_method(D_METHOD("get_backlight"), &FoliageMaterial::get_backlight);
	ClassDB::bind_method(D_METHOD("set_sway", "sway"), &FoliageMaterial::set_sway);
	ClassDB::bind_method(D_METHOD("get_sway"), &FoliageMaterial::get_sway);
	ClassDB::bind_method(D_METHOD("set_sway_height", "height"), &FoliageMaterial::set_sway_height);
	ClassDB::bind_method(D_METHOD("get_sway_height"), &FoliageMaterial::get_sway_height);
	ClassDB::bind_method(D_METHOD("set_stiffness", "stiffness"), &FoliageMaterial::set_stiffness);
	ClassDB::bind_method(D_METHOD("get_stiffness"), &FoliageMaterial::get_stiffness);

	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "albedo_color"), "set_albedo_color", "get_albedo_color");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "albedo_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_albedo_texture", "get_albedo_texture");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "alpha_scissor_threshold", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_alpha_scissor_threshold", "get_alpha_scissor_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "normal_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_normal_texture", "get_normal_texture");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "normal_strength", PROPERTY_HINT_RANGE, "-16,16,0.01"), "set_normal_strength", "get_normal_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "roughness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_roughness", "get_roughness");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "backlight", PROPERTY_HINT_COLOR_NO_ALPHA), "set_backlight", "get_backlight");
	ADD_GROUP("Wind", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sway", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_sway", "get_sway");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sway_height", PROPERTY_HINT_RANGE, "0.01,40,0.01,or_greater,suffix:m"), "set_sway_height", "get_sway_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "stiffness", PROPERTY_HINT_RANGE, "0.05,8,0.01"), "set_stiffness", "get_stiffness");
}
