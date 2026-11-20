#include "baked_animation_material.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

Mutex BakedAnimationMaterial::shader_mutex;
RID BakedAnimationMaterial::shader;

namespace {
const char *BAKED_ANIMATION_SHADER = R"(
shader_type spatial;

uniform sampler2D baked_positions : filter_nearest;
uniform sampler2D baked_normals : filter_nearest;
uniform int texture_width = 1;
uniform int rows_per_frame = 1;
uniform vec4 albedo_color : source_color;
uniform sampler2D albedo_texture : source_color, hint_default_white, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float roughness : hint_range(0.0, 1.0);

ivec2 texel(int frame, int vertex) {
	return ivec2(vertex % texture_width, frame * rows_per_frame + vertex / texture_width);
}

void vertex() {
	// The copy's custom data: the animation's first frame, how many frames, where in them, and
	// whether it loops (BakedAnimations.get_custom_data()).
	float first = INSTANCE_CUSTOM.x;
	float frames = max(INSTANCE_CUSTOM.y, 1.0);
	float at = INSTANCE_CUSTOM.z;
	float from = floor(at);
	float next = INSTANCE_CUSTOM.w > 0.5 ? mod(from + 1.0, frames) : min(from + 1.0, frames - 1.0);
	int a = int(first + from);
	int b = int(first + next);
	float t = at - from;
	VERTEX = mix(texelFetch(baked_positions, texel(a, VERTEX_ID), 0).xyz, texelFetch(baked_positions, texel(b, VERTEX_ID), 0).xyz, t);
	NORMAL = normalize(mix(texelFetch(baked_normals, texel(a, VERTEX_ID), 0).xyz, texelFetch(baked_normals, texel(b, VERTEX_ID), 0).xyz, t));
}

void fragment() {
	vec4 albedo = albedo_color * texture(albedo_texture, UV);
	ALBEDO = albedo.rgb;
	ROUGHNESS = roughness;
}
)";

void set_texture(RID p_material, const StringName &p_param, const Ref<Texture2D> &p_texture) {
	RS::get_singleton()->material_set_param(p_material, p_param, p_texture.is_valid() ? Variant(p_texture->get_rid()) : Variant());
}
} // namespace

BakedAnimationMaterial::BakedAnimationMaterial() {
	_set_material(RS::get_singleton()->material_create());
	set_albedo_color(albedo_color);
	set_roughness(roughness);
}

void BakedAnimationMaterial::_update_shader() {
	MutexLock lock(shader_mutex);
	if (shader.is_null()) {
		shader = RS::get_singleton()->shader_create();
		RS::get_singleton()->shader_set_code(shader, BAKED_ANIMATION_SHADER);
	}
}

void BakedAnimationMaterial::cleanup_shader() {
	if (shader.is_valid()) {
		RS::get_singleton()->free_rid(shader);
		shader = RID();
	}
}

Shader::Mode BakedAnimationMaterial::get_shader_mode() const {
	return Shader::MODE_SPATIAL;
}

RID BakedAnimationMaterial::get_shader_rid() const {
	_update_shader();
	return shader;
}

RID BakedAnimationMaterial::get_rid() const {
	_update_shader();
	if (!shader_set) {
		RS::get_singleton()->material_set_shader(_get_material(), shader);
		shader_set = true;
	}
	return _get_material();
}

void BakedAnimationMaterial::_baked_changed() {
	const RID material = _get_material();
	if (baked_animations.is_null()) {
		set_texture(material, "baked_positions", Ref<Texture2D>());
		set_texture(material, "baked_normals", Ref<Texture2D>());
		return;
	}
	set_texture(material, "baked_positions", baked_animations->get_positions());
	set_texture(material, "baked_normals", baked_animations->get_normals());
	RS::get_singleton()->material_set_param(material, "texture_width", MAX(baked_animations->get_texture_width(), 1));
	RS::get_singleton()->material_set_param(material, "rows_per_frame", MAX(baked_animations->get_rows_per_frame(), 1));
}

void BakedAnimationMaterial::set_baked_animations(const Ref<BakedAnimations> &p_baked) {
	if (baked_animations.is_valid()) {
		baked_animations->disconnect_changed(callable_mp(this, &BakedAnimationMaterial::_baked_changed));
	}
	baked_animations = p_baked;
	if (baked_animations.is_valid()) {
		baked_animations->connect_changed(callable_mp(this, &BakedAnimationMaterial::_baked_changed));
	}
	_baked_changed();
}

Ref<BakedAnimations> BakedAnimationMaterial::get_baked_animations() const {
	return baked_animations;
}

void BakedAnimationMaterial::set_albedo_color(const Color &p_color) {
	albedo_color = p_color;
	RS::get_singleton()->material_set_param(_get_material(), "albedo_color", albedo_color);
}

Color BakedAnimationMaterial::get_albedo_color() const {
	return albedo_color;
}

void BakedAnimationMaterial::set_albedo_texture(const Ref<Texture2D> &p_texture) {
	albedo_texture = p_texture;
	set_texture(_get_material(), "albedo_texture", albedo_texture);
}

Ref<Texture2D> BakedAnimationMaterial::get_albedo_texture() const {
	return albedo_texture;
}

void BakedAnimationMaterial::set_roughness(float p_roughness) {
	roughness = CLAMP(p_roughness, 0.0f, 1.0f);
	RS::get_singleton()->material_set_param(_get_material(), "roughness", roughness);
}

float BakedAnimationMaterial::get_roughness() const {
	return roughness;
}

void BakedAnimationMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_baked_animations", "baked_animations"), &BakedAnimationMaterial::set_baked_animations);
	ClassDB::bind_method(D_METHOD("get_baked_animations"), &BakedAnimationMaterial::get_baked_animations);
	ClassDB::bind_method(D_METHOD("set_albedo_color", "color"), &BakedAnimationMaterial::set_albedo_color);
	ClassDB::bind_method(D_METHOD("get_albedo_color"), &BakedAnimationMaterial::get_albedo_color);
	ClassDB::bind_method(D_METHOD("set_albedo_texture", "texture"), &BakedAnimationMaterial::set_albedo_texture);
	ClassDB::bind_method(D_METHOD("get_albedo_texture"), &BakedAnimationMaterial::get_albedo_texture);
	ClassDB::bind_method(D_METHOD("set_roughness", "roughness"), &BakedAnimationMaterial::set_roughness);
	ClassDB::bind_method(D_METHOD("get_roughness"), &BakedAnimationMaterial::get_roughness);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "baked_animations", PROPERTY_HINT_RESOURCE_TYPE, "BakedAnimations"), "set_baked_animations", "get_baked_animations");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "albedo_color"), "set_albedo_color", "get_albedo_color");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "albedo_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_albedo_texture", "get_albedo_texture");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "roughness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_roughness", "get_roughness");
}
