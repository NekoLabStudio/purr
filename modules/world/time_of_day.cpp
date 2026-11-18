#include "time_of_day.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "scene/resources/3d/world_3d.h"

namespace {
const Color LOW_SUN = Color(1.0, 0.55, 0.3); // Dawn and dusk.
const Color HIGH_SUN = Color(1.0, 0.97, 0.92);
const Color MOON = Color(0.65, 0.75, 1.0);
} //namespace

TimeOfDay::TimeOfDay() {
}

void TimeOfDay::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			set_notify_transform(true); // Turning it turns the compass.
			set_process(!Engine::get_singleton()->is_editor_hint());
			_apply();
		} break;
		case NOTIFICATION_TRANSFORM_CHANGED: {
			_apply();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			// The sky goes back to its own brightness.
			if (environment.is_valid()) {
				environment->set_bg_energy_multiplier(day_background_energy);
				environment->set_ambient_light_energy(day_ambient_energy);
				environment.unref();
			}
		} break;
		case NOTIFICATION_PROCESS: {
			if (day_length <= 0.0f) {
				break;
			}
			// Every hour that begins, in order, even when a frame spans several (a short day, a hitch).
			const float before = time;
			const float passed = float(get_process_delta_time()) * 24.0f / day_length;
			time = Math::fposmod(time + passed, 24.0f);
			_apply();
			const int first = int(Math::floor(before));
			const int crossed = int(Math::floor(before + passed)) - first;
			// After a long pause on a short day, the last day's hours.
			for (int i = MAX(crossed - 23, 1); i <= crossed; i++) {
				emit_signal(SNAME("hour_changed"), (first + i) % 24);
			}
		} break;
	}
}

// A set light, or else its first DirectionalLight3D child that isn't the other light.
DirectionalLight3D *TimeOfDay::_light(const NodePath &p_path, const DirectionalLight3D *p_other) const {
	if (!p_path.is_empty()) {
		return Object::cast_to<DirectionalLight3D>(get_node_or_null(p_path));
	}
	for (int i = 0; i < get_child_count(); i++) {
		DirectionalLight3D *light = Object::cast_to<DirectionalLight3D>(get_child(i));
		if (light != nullptr && light != p_other) {
			return light;
		}
	}
	return nullptr;
}

Ref<Environment> TimeOfDay::_environment() const {
	const Ref<World3D> world = is_inside_tree() ? get_world_3d() : Ref<World3D>();
	return world.is_valid() ? world->get_environment() : Ref<Environment>();
}

// Points a light from where it is in the sky, at the given strength and colour; out of sight
// below the horizon, it's off, so its shadows cost nothing.
void TimeOfDay::_place_light(DirectionalLight3D *p_light, const Vector3 &p_towards, float p_energy, const Color &p_color) {
	if (p_light == nullptr) {
		return;
	}
	const Vector3 up = Math::abs(p_towards.y) > 0.99f ? Vector3(0, 0, -1) : Vector3(0, 1, 0);
	p_light->set_global_basis(Basis::looking_at(-p_towards, up));
	p_light->set_param(Light3D::PARAM_ENERGY, p_energy);
	p_light->set_color(p_color);
	p_light->set_visible(p_energy > 0.001f);
}

void TimeOfDay::_apply() {
	if (!is_inside_tree()) {
		return;
	}
	const Vector3 sun = get_sun_direction();
	const float sun_up = Math::smoothstep(-0.02f, 0.1f, sun.y);
	const float moon_up = Math::smoothstep(-0.02f, 0.1f, -sun.y);
	DirectionalLight3D *sun_light = _light(sun_path, nullptr);
	_place_light(sun_light, sun, sun_energy * sun_up, LOW_SUN.lerp(HIGH_SUN, Math::smoothstep(0.0f, 0.4f, sun.y)));
	_place_light(_light(moon_path, sun_light), -sun, moon_energy * moon_up, MOON);

	// Only in a game: in the editor, the environment would be saved dimmed.
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	const Ref<Environment> current = _environment();
	if (current != environment) {
		environment = current;
		if (environment.is_valid()) {
			day_background_energy = environment->get_bg_energy_multiplier();
			day_ambient_energy = environment->get_ambient_light_energy();
		}
	}
	if (environment.is_valid()) {
		const float sky = Math::lerp(night_sky_energy, 1.0f, get_daylight());
		environment->set_bg_energy_multiplier(day_background_energy * sky);
		environment->set_ambient_light_energy(day_ambient_energy * sky);
	}
}

// Towards the sun, in global space: a sun at the equinox, rising in the east (the node's +X) and
// at noon due south (+Z) as far up as latitude allows.
Vector3 TimeOfDay::get_sun_direction() const {
	const float hour_angle = (time - 12.0f) / 24.0f * Math::TAU;
	const float lat = Math::deg_to_rad(latitude);
	const Vector3 local = Vector3(-Math::sin(hour_angle), Math::cos(lat) * Math::cos(hour_angle), Math::sin(lat) * Math::cos(hour_angle));
	return (is_inside_tree() ? get_global_basis() : get_basis()).orthonormalized().xform(local).normalized();
}

float TimeOfDay::get_daylight() const {
	return Math::smoothstep(-0.1f, 0.1f, get_sun_direction().y);
}

bool TimeOfDay::is_day() const {
	return get_sun_direction().y > 0.0f;
}

void TimeOfDay::set_time(float p_time) {
	time = Math::fposmod(p_time, 24.0f);
	_apply();
}

float TimeOfDay::get_time() const {
	return time;
}

void TimeOfDay::set_day_length(float p_seconds) {
	day_length = MAX(p_seconds, 0.0f);
}

float TimeOfDay::get_day_length() const {
	return day_length;
}

void TimeOfDay::set_latitude(float p_degrees) {
	latitude = CLAMP(p_degrees, -90.0f, 90.0f);
	_apply();
}

float TimeOfDay::get_latitude() const {
	return latitude;
}

void TimeOfDay::set_sun_energy(float p_energy) {
	sun_energy = MAX(p_energy, 0.0f);
	_apply();
}

float TimeOfDay::get_sun_energy() const {
	return sun_energy;
}

void TimeOfDay::set_moon_energy(float p_energy) {
	moon_energy = MAX(p_energy, 0.0f);
	_apply();
}

float TimeOfDay::get_moon_energy() const {
	return moon_energy;
}

void TimeOfDay::set_night_sky_energy(float p_energy) {
	night_sky_energy = CLAMP(p_energy, 0.0f, 1.0f);
	_apply();
}

float TimeOfDay::get_night_sky_energy() const {
	return night_sky_energy;
}

void TimeOfDay::set_sun(const NodePath &p_path) {
	sun_path = p_path;
	_apply();
}

NodePath TimeOfDay::get_sun() const {
	return sun_path;
}

void TimeOfDay::set_moon(const NodePath &p_path) {
	moon_path = p_path;
	_apply();
}

NodePath TimeOfDay::get_moon() const {
	return moon_path;
}

void TimeOfDay::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_time", "time"), &TimeOfDay::set_time);
	ClassDB::bind_method(D_METHOD("get_time"), &TimeOfDay::get_time);
	ClassDB::bind_method(D_METHOD("set_day_length", "seconds"), &TimeOfDay::set_day_length);
	ClassDB::bind_method(D_METHOD("get_day_length"), &TimeOfDay::get_day_length);
	ClassDB::bind_method(D_METHOD("set_latitude", "degrees"), &TimeOfDay::set_latitude);
	ClassDB::bind_method(D_METHOD("get_latitude"), &TimeOfDay::get_latitude);
	ClassDB::bind_method(D_METHOD("set_sun_energy", "energy"), &TimeOfDay::set_sun_energy);
	ClassDB::bind_method(D_METHOD("get_sun_energy"), &TimeOfDay::get_sun_energy);
	ClassDB::bind_method(D_METHOD("set_moon_energy", "energy"), &TimeOfDay::set_moon_energy);
	ClassDB::bind_method(D_METHOD("get_moon_energy"), &TimeOfDay::get_moon_energy);
	ClassDB::bind_method(D_METHOD("set_night_sky_energy", "energy"), &TimeOfDay::set_night_sky_energy);
	ClassDB::bind_method(D_METHOD("get_night_sky_energy"), &TimeOfDay::get_night_sky_energy);
	ClassDB::bind_method(D_METHOD("set_sun", "path"), &TimeOfDay::set_sun);
	ClassDB::bind_method(D_METHOD("get_sun"), &TimeOfDay::get_sun);
	ClassDB::bind_method(D_METHOD("set_moon", "path"), &TimeOfDay::set_moon);
	ClassDB::bind_method(D_METHOD("get_moon"), &TimeOfDay::get_moon);
	ClassDB::bind_method(D_METHOD("get_sun_direction"), &TimeOfDay::get_sun_direction);
	ClassDB::bind_method(D_METHOD("get_daylight"), &TimeOfDay::get_daylight);
	ClassDB::bind_method(D_METHOD("is_day"), &TimeOfDay::is_day);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time", PROPERTY_HINT_RANGE, "0,24,0.01,suffix:h"), "set_time", "get_time");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "day_length", PROPERTY_HINT_RANGE, "0,3600,1,or_greater,suffix:s"), "set_day_length", "get_day_length");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "latitude", PROPERTY_HINT_RANGE, "-90,90,0.1,degrees"), "set_latitude", "get_latitude");
	ADD_GROUP("Lights", "");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sun", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "DirectionalLight3D"), "set_sun", "get_sun");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sun_energy", PROPERTY_HINT_RANGE, "0,16,0.01,or_greater"), "set_sun_energy", "get_sun_energy");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "moon", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "DirectionalLight3D"), "set_moon", "get_moon");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "moon_energy", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_moon_energy", "get_moon_energy");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "night_sky_energy", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_night_sky_energy", "get_night_sky_energy");

	ADD_SIGNAL(MethodInfo("hour_changed", PropertyInfo(Variant::INT, "hour")));
}
