#include "interior_material.h"

#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

#include "modules/graphics/sky_globals.h"

Mutex InteriorMaterial::shader_mutex;
RID InteriorMaterial::shader;

namespace {
const char *INTERIOR_SHADER = R"(
shader_type spatial;
render_mode cull_back;

uniform vec2 rooms;
uniform float room_depth;
uniform sampler2D facade_texture : source_color, hint_default_transparent, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float frame_width;
uniform vec3 frame_color : source_color;
uniform float lit_rooms;
uniform vec3 light_color : source_color;
uniform float light_energy;

global uniform float time_of_day_night;

float room_hash(vec2 cell, float salt) {
	uvec2 q = uvec2(ivec2(cell) + 65536);
	uint v = (q.x * 747796405u) ^ (q.y * 2891336453u) ^ uint(salt * 1013.0) * 277803737u;
	v = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
	return float((v >> 22u) ^ v) * (1.0 / 4294967296.0);
}

float to_wall(float at, float going) {
	if (abs(going) < 1e-5) {
		return 1e6;
	}
	return ((going > 0.0 ? 1.0 : 0.0) - at) / going;
}

void fragment() {
	vec4 facade = texture(facade_texture, UV);
	vec2 across = UV * rooms;
	// Each window its own room, different on each building too.
	vec2 cell = floor(across) + floor(NODE_POSITION_WORLD.xz * 0.37) * 131.0;
	vec2 local = fract(across);
	float framed = max(step(local.x, frame_width) + step(1.0 - frame_width, local.x), step(local.y, frame_width) + step(1.0 - frame_width, local.y));
	float glass = (1.0 - facade.a) * (1.0 - min(framed, 1.0));

	// The way the camera looks in, in the window's own terms: across, down and into the room.
	vec3 into = -VIEW;
	vec3 ray = vec3(dot(into, normalize(TANGENT)), dot(into, normalize(BINORMAL)), dot(into, normalize(NORMAL)));
	vec3 room = vec3(0.0);
	float height = 0.5; // How far up the room what's seen is (1 at the ceiling).
	if (ray.z < -1e-4 && glass > 0.0) {
		vec3 start = vec3(local, 0.0);
		float depth = room_depth;
		float t_side = to_wall(start.x, ray.x);
		float t_level = to_wall(start.y, ray.y);
		float t_back = -depth / ray.z;
		float t = min(t_back, min(t_side, t_level));
		vec3 hit = start + ray * t;
		height = 1.0 - hit.y;
		vec3 wall = mix(vec3(0.78, 0.73, 0.64), vec3(0.62, 0.7, 0.76), room_hash(cell, 1.0)) * mix(0.7, 1.0, room_hash(cell, 2.0));
		vec3 floor_colour = mix(vec3(0.36, 0.25, 0.16), vec3(0.42, 0.42, 0.43), step(0.6, room_hash(cell, 3.0)));
		if (t == t_back) {
			room = wall * 0.85;
		} else if (t == t_side) {
			room = wall * 0.7;
		} else {
			room = ray.y > 0.0 ? floor_colour : vec3(0.86);
		}
		// Something standing in the room: a dark shape across part of the floor.
		float t_furniture = -depth * 0.55 / ray.z;
		vec3 furniture = start + ray * t_furniture;
		float from = room_hash(cell, 4.0) * 0.6;
		if (t_furniture < t && furniture.x > from && furniture.x < from + 0.35 && furniture.y > 0.68) {
			room = vec3(0.12, 0.11, 0.1) + 0.1 * room_hash(cell, 5.0);
			height = 0.2;
		}
		// Blinds partly down in some windows.
		float blind = room_hash(cell, 6.0) < 0.35 ? room_hash(cell, 7.0) * 0.7 : 0.0;
		if (local.y < blind) {
			room = vec3(0.82, 0.79, 0.72);
			height = 0.9;
		}
	}
	// Lights come on as it gets dark, a few rooms at a time; some cooler than others.
	float on = step(room_hash(cell, 8.0), lit_rooms) * step(0.15 + 0.7 * room_hash(cell, 9.0), time_of_day_night);
	vec3 lamp = mix(light_color, vec3(0.85, 0.9, 1.0), step(0.75, room_hash(cell, 10.0)));
	vec3 lit = room * lamp * light_energy * (0.55 + 0.45 * height) * on;
	vec3 daylight = room * 0.12 * (1.0 - time_of_day_night);

	vec3 surface = mix(frame_color, facade.rgb, facade.a);
	ALBEDO = mix(surface, vec3(0.02), glass);
	ROUGHNESS = mix(mix(0.6, 0.85, facade.a), 0.04, glass);
	SPECULAR = 0.5;
	EMISSION = (lit + daylight) * glass;
}
)";
} // namespace

InteriorMaterial::InteriorMaterial() {
	_set_material(RS::get_singleton()->material_create());
	set_rooms(rooms);
	set_room_depth(room_depth);
	set_frame_width(frame_width);
	set_frame_color(frame_color);
	set_lit_rooms(lit_rooms);
	set_light_color(light_color);
	set_light_energy(light_energy);
}

void InteriorMaterial::_update_shader() {
	MutexLock lock(shader_mutex);
	if (shader.is_null()) {
		SkyGlobals::ensure();
		shader = RS::get_singleton()->shader_create();
		RS::get_singleton()->shader_set_code(shader, INTERIOR_SHADER);
	}
}

void InteriorMaterial::cleanup_shader() {
	if (shader.is_valid()) {
		RS::get_singleton()->free_rid(shader);
		shader = RID();
	}
}

Shader::Mode InteriorMaterial::get_shader_mode() const {
	return Shader::MODE_SPATIAL;
}

RID InteriorMaterial::get_shader_rid() const {
	_update_shader();
	return shader;
}

RID InteriorMaterial::get_rid() const {
	_update_shader();
	if (!shader_set) {
		RS::get_singleton()->material_set_shader(_get_material(), shader);
		shader_set = true;
	}
	return _get_material();
}

void InteriorMaterial::set_rooms(const Vector2i &p_rooms) {
	rooms = p_rooms.maxi(1);
	RS::get_singleton()->material_set_param(_get_material(), "rooms", Vector2(rooms));
}

Vector2i InteriorMaterial::get_rooms() const {
	return rooms;
}

void InteriorMaterial::set_room_depth(float p_depth) {
	room_depth = MAX(p_depth, 0.05f);
	RS::get_singleton()->material_set_param(_get_material(), "room_depth", room_depth);
}

float InteriorMaterial::get_room_depth() const {
	return room_depth;
}

void InteriorMaterial::set_facade_texture(const Ref<Texture2D> &p_texture) {
	facade_texture = p_texture;
	RS::get_singleton()->material_set_param(_get_material(), "facade_texture", facade_texture.is_valid() ? Variant(facade_texture->get_rid()) : Variant());
}

Ref<Texture2D> InteriorMaterial::get_facade_texture() const {
	return facade_texture;
}

void InteriorMaterial::set_frame_width(float p_width) {
	frame_width = CLAMP(p_width, 0.0f, 0.5f);
	RS::get_singleton()->material_set_param(_get_material(), "frame_width", frame_width);
}

float InteriorMaterial::get_frame_width() const {
	return frame_width;
}

void InteriorMaterial::set_frame_color(const Color &p_color) {
	frame_color = p_color;
	RS::get_singleton()->material_set_param(_get_material(), "frame_color", frame_color);
}

Color InteriorMaterial::get_frame_color() const {
	return frame_color;
}

void InteriorMaterial::set_lit_rooms(float p_fraction) {
	lit_rooms = CLAMP(p_fraction, 0.0f, 1.0f);
	RS::get_singleton()->material_set_param(_get_material(), "lit_rooms", lit_rooms);
}

float InteriorMaterial::get_lit_rooms() const {
	return lit_rooms;
}

void InteriorMaterial::set_light_color(const Color &p_color) {
	light_color = p_color;
	RS::get_singleton()->material_set_param(_get_material(), "light_color", light_color);
}

Color InteriorMaterial::get_light_color() const {
	return light_color;
}

void InteriorMaterial::set_light_energy(float p_energy) {
	light_energy = MAX(p_energy, 0.0f);
	RS::get_singleton()->material_set_param(_get_material(), "light_energy", light_energy);
}

float InteriorMaterial::get_light_energy() const {
	return light_energy;
}

void InteriorMaterial::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_rooms", "rooms"), &InteriorMaterial::set_rooms);
	ClassDB::bind_method(D_METHOD("get_rooms"), &InteriorMaterial::get_rooms);
	ClassDB::bind_method(D_METHOD("set_room_depth", "depth"), &InteriorMaterial::set_room_depth);
	ClassDB::bind_method(D_METHOD("get_room_depth"), &InteriorMaterial::get_room_depth);
	ClassDB::bind_method(D_METHOD("set_facade_texture", "texture"), &InteriorMaterial::set_facade_texture);
	ClassDB::bind_method(D_METHOD("get_facade_texture"), &InteriorMaterial::get_facade_texture);
	ClassDB::bind_method(D_METHOD("set_frame_width", "width"), &InteriorMaterial::set_frame_width);
	ClassDB::bind_method(D_METHOD("get_frame_width"), &InteriorMaterial::get_frame_width);
	ClassDB::bind_method(D_METHOD("set_frame_color", "color"), &InteriorMaterial::set_frame_color);
	ClassDB::bind_method(D_METHOD("get_frame_color"), &InteriorMaterial::get_frame_color);
	ClassDB::bind_method(D_METHOD("set_lit_rooms", "fraction"), &InteriorMaterial::set_lit_rooms);
	ClassDB::bind_method(D_METHOD("get_lit_rooms"), &InteriorMaterial::get_lit_rooms);
	ClassDB::bind_method(D_METHOD("set_light_color", "color"), &InteriorMaterial::set_light_color);
	ClassDB::bind_method(D_METHOD("get_light_color"), &InteriorMaterial::get_light_color);
	ClassDB::bind_method(D_METHOD("set_light_energy", "energy"), &InteriorMaterial::set_light_energy);
	ClassDB::bind_method(D_METHOD("get_light_energy"), &InteriorMaterial::get_light_energy);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2I, "rooms"), "set_rooms", "get_rooms");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "room_depth", PROPERTY_HINT_RANGE, "0.05,4,0.01,or_greater"), "set_room_depth", "get_room_depth");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "facade_texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_facade_texture", "get_facade_texture");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "frame_width", PROPERTY_HINT_RANGE, "0,0.5,0.001"), "set_frame_width", "get_frame_width");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "frame_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_frame_color", "get_frame_color");
	ADD_GROUP("Lights", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lit_rooms", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_lit_rooms", "get_lit_rooms");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "light_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_light_color", "get_light_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "light_energy", PROPERTY_HINT_RANGE, "0,16,0.01,or_greater"), "set_light_energy", "get_light_energy");
}
