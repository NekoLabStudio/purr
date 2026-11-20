#include "impostor_material.h"

#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

Mutex ImpostorMaterial::shader_mutex;
RID ImpostorMaterial::shader;

namespace {
// The views are laid out over the upper half of a sphere as a hemi-octahedral map: each cell of
// the grid is one direction the node was baked from (Impostor.bake()).
const char *IMPOSTOR_SHADER = R"(
shader_type spatial;
render_mode cull_disabled;

uniform sampler2D impostor_texture : source_color, filter_linear_mipmap, repeat_disable;
uniform int grid = 8;
uniform vec3 center;
uniform float radius = 1.0;
uniform float alpha_scissor_threshold : hint_range(0.0, 1.0) = 0.5;

varying vec2 cell_uv;
varying vec2 quad;

vec2 hemi_octahedral(vec3 direction) {
	vec3 d = direction / (abs(direction.x) + abs(direction.y) + abs(direction.z));
	return vec2(d.x + d.z, d.x - d.z) * 0.5 + 0.5;
}

void vertex() {
	// Towards the camera (or, drawing shadows, the light), in the node's own space.
	vec3 eye = (inverse(MODEL_MATRIX) * vec4(CAMERA_POSITION_WORLD, 1.0)).xyz;
	vec3 towards = normalize(eye - center);
	vec3 seen = normalize(vec3(towards.x, max(towards.y, 0.0), towards.z));
	vec2 cell = clamp(floor(hemi_octahedral(seen) * float(grid)), vec2(0.0), vec2(float(grid) - 1.0));
	// The quad turned to face it, as large as the node.
	vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), towards) + vec3(1e-5, 0.0, 0.0));
	vec3 up = cross(towards, right);
	quad = VERTEX.xy;
	VERTEX = center + (right * VERTEX.x + up * VERTEX.y) * radius;
	NORMAL = towards;
	cell_uv = (cell + vec2(UV.x, UV.y)) / float(grid);
}

void fragment() {
	vec4 colour = texture(impostor_texture, cell_uv);
	ALBEDO = colour.rgb;
	ALPHA = colour.a;
	ALPHA_SCISSOR_THRESHOLD = alpha_scissor_threshold;
	// Rounded, like a tree's crown: lit softly from the side the light comes from.
	vec2 q = clamp(quad, vec2(-1.0), vec2(1.0));
	NORMAL = normalize(vec3(q.x, q.y, sqrt(max(1.0 - dot(q, q), 0.0)) + 0.35));
	ROUGHNESS = 0.9;
}
)";
} // namespace

ImpostorMaterial::ImpostorMaterial() {
	_set_material(RS::get_singleton()->material_create());
	set_grid(grid);
	set_center(center);
	set_radius(radius);
	set_alpha_scissor_threshold(alpha_scissor_threshold);
}

void ImpostorMaterial::_update_shader() {
	MutexLock lock(shader_mutex);
	if (shader.is_null()) {
		shader = RS::get_singleton()->shader_create();
		RS::get_singleton()->shader_set_code(shader, IMPOSTOR_SHADER);
	}
}

void ImpostorMaterial::cleanup_shader() {
	if (shader.is_valid()) {
		RS::get_singleton()->free_rid(shader);
		shader = RID();
	}
}

Shader::Mode ImpostorMaterial::get_shader_mode() const {
	return Shader::MODE_SPATIAL;
}

RID ImpostorMaterial::get_shader_rid() const {
	_update_shader();
	return shader;
}

RID ImpostorMaterial::get_rid() const {
	_update_shader();
	if (!shader_set) {
		RS::get_singleton()->material_set_shader(_get_material(), shader);
		shader_set = true;
	}
	return _get_material();
}

void ImpostorMaterial::set_texture(const Ref<Texture2D> &p_texture) {
	texture = p_texture;
	RS::get_singleton()->material_set_param(_get_material(), "impostor_texture", texture.is_valid() ? Variant(texture->get_rid()) : Variant());
}

Ref<Texture2D> ImpostorMaterial::get_texture() const {
	return texture;
}

void ImpostorMaterial::set_grid(int p_grid) {
	grid = MAX(p_grid, 1);
	RS::get_singleton()->material_set_param(_get_material(), "grid", grid);
}

int ImpostorMaterial::get_grid() const {
	return grid;
}

void ImpostorMaterial::set_center(const Vector3 &p_center) {
	center = p_center;
	RS::get_singleton()->material_set_param(_get_material(), "center", center);
}

Vector3 ImpostorMaterial::get_center() const {
	return center;
}

void ImpostorMaterial::set_radius(float p_radius) {
	radius = MAX(p_radius, 0.001f);
	RS::get_singleton()->material_set_param(_get_material(), "radius", radius);
}

float ImpostorMaterial::get_radius() const {
	return radius;
}

void ImpostorMaterial::set_alpha_scissor_threshold(float p_threshold) {
	alpha_scissor_threshold = CLAMP(p_threshold, 0.0f, 1.0f);
	RS::get_singleton()->material_set_param(_get_material(), "alpha_scissor_threshold", alpha_scissor_threshold);
}

float ImpostorMaterial::get_alpha_scissor_threshold() const {
	return alpha_scissor_threshold;
}

void ImpostorMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_texture", "texture"), &ImpostorMaterial::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture"), &ImpostorMaterial::get_texture);
	ClassDB::bind_method(D_METHOD("set_grid", "grid"), &ImpostorMaterial::set_grid);
	ClassDB::bind_method(D_METHOD("get_grid"), &ImpostorMaterial::get_grid);
	ClassDB::bind_method(D_METHOD("set_center", "center"), &ImpostorMaterial::set_center);
	ClassDB::bind_method(D_METHOD("get_center"), &ImpostorMaterial::get_center);
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &ImpostorMaterial::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &ImpostorMaterial::get_radius);
	ClassDB::bind_method(D_METHOD("set_alpha_scissor_threshold", "threshold"), &ImpostorMaterial::set_alpha_scissor_threshold);
	ClassDB::bind_method(D_METHOD("get_alpha_scissor_threshold"), &ImpostorMaterial::get_alpha_scissor_threshold);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_texture", "get_texture");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "grid", PROPERTY_HINT_RANGE, "1,32,1"), "set_grid", "get_grid");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "center", PROPERTY_HINT_NONE, "suffix:m"), "set_center", "get_center");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.001,100,0.001,or_greater,suffix:m"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "alpha_scissor_threshold", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_alpha_scissor_threshold", "get_alpha_scissor_threshold");
}
