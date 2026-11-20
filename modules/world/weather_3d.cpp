#include "weather_3d.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/gpu_particles_collision_3d.h"
#include "scene/3d/light_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/particle_process_material.h"

#include "modules/graphics/sky_globals.h"
#include "modules/graphics/weather_shelter.h"
#include "time_of_day.h"
#include "world_origin.h"

namespace {
// The clouds' plane moves this far a second for each metre a second of wind.
constexpr float DRIFT_PER_WIND = 0.0015;
// Rain and snow need cloud: this much at least while they fall.
constexpr float RAIN_CLOUD = 0.95;
// The height map of what's overhead: this big round the camera, moved in steps this long.
const Vector3 SHELTER_SIZE = Vector3(64.0, 64.0, 64.0);
constexpr float SHELTER_STEP = 4.0;
// The fog that rain and snow bring by themselves.
constexpr float RAIN_FOG = 0.35;
constexpr float SNOW_FOG = 0.55;
} // namespace

Weather3D::Weather3D() {
	random.instantiate();
	random->randomize();
}

void Weather3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			settle();
			if (!Engine::get_singleton()->is_editor_hint()) {
				_make_weather();
				if (WorldOrigin::get_singleton() != nullptr) {
					WorldOrigin::get_singleton()->connect(SNAME("shifted"), callable_mp(this, &Weather3D::_restart_drops));
				}
			}
			set_process(true);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (WorldOrigin::get_singleton() != nullptr && WorldOrigin::get_singleton()->is_connected(SNAME("shifted"), callable_mp(this, &Weather3D::_restart_drops))) {
				WorldOrigin::get_singleton()->disconnect(SNAME("shifted"), callable_mp(this, &Weather3D::_restart_drops));
			}
			_restore_fog();
			if (shelter != nullptr) {
				WeatherShelter::set(WeatherShelter::Shelter());
			}
			SkyGlobals::set(SkyGlobals::LIGHTNING, 0.0);
		} break;
		case NOTIFICATION_PROCESS: {
			if (Engine::get_singleton()->is_editor_hint()) {
				settle(); // The editor previews the weather as set.
			} else {
				_step(float(get_process_delta_time()));
			}
		} break;
	}
}

// Particles falling round the camera, lit like everything else, so rain is grey by day and catches
// the lights at night. They end where they meet a particle collider: the shelter's height map
// keeps them off the ground under roofs.
GPUParticles3D *Weather3D::_make_particles(Node *p_parent, const Vector3 &p_box, float p_lifetime, const Size2 &p_size, const Color &p_color, bool p_streak) {
	GPUParticles3D *particles = memnew(GPUParticles3D);
	particles->set_lifetime(p_lifetime);
	particles->set_pre_process_time(p_lifetime);
	particles->set_use_local_coordinates(false);
	particles->set_visibility_aabb(AABB(-p_box - Vector3(0.0, 30.0, 0.0), (p_box + Vector3(0.0, 30.0, 0.0)) * 2.0));
	Ref<ParticleProcessMaterial> process;
	process.instantiate();
	process->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_BOX);
	process->set_emission_box_extents(p_box);
	process->set_direction(Vector3(0.0, -1.0, 0.0));
	process->set_collision_mode(ParticleProcessMaterial::COLLISION_HIDE_ON_CONTACT);
	particles->set_process_material(process);
	Ref<QuadMesh> quad;
	quad.instantiate();
	quad->set_size(p_size);
	Ref<StandardMaterial3D> look;
	look.instantiate();
	look->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	look->set_albedo(p_color);
	look->set_roughness(p_streak ? 0.2 : 0.9);
	look->set_billboard_mode(p_streak ? BaseMaterial3D::BILLBOARD_FIXED_Y : BaseMaterial3D::BILLBOARD_ENABLED);
	look->set_flag(BaseMaterial3D::FLAG_BILLBOARD_KEEP_SCALE, true);
	quad->set_material(look);
	particles->set_draw_pass_mesh(0, quad);
	particles->set_emitting(false);
	p_parent->add_child(particles, false, INTERNAL_MODE_BACK);
	return particles;
}

void Weather3D::_make_weather() {
	drops = _make_particles(this, Vector3(22.0, 0.5, 22.0), 0.9, Size2(0.012, 0.55), Color(0.85, 0.88, 0.92, 0.25), true);
	drops->set_amount(rain_amount);
	Ref<ParticleProcessMaterial> rain_process = drops->get_process_material();
	rain_process->set_spread(2.0);
	rain_process->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 16.0);
	rain_process->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 20.0);
	rain_process->set_gravity(Vector3(0.0, -2.0, 0.0));
	// Where a drop lands, a little splash: moving with the rain, so it's drawn where the rain is.
	GPUParticles3D *splashes = _make_particles(drops, Vector3(22.0, 0.5, 22.0), 0.3, Size2(0.035, 0.035), Color(0.85, 0.88, 0.92, 0.35), false);
	splashes->set_amount(MAX(rain_amount / 4, 1));
	Ref<ParticleProcessMaterial> splash_process = splashes->get_process_material();
	splash_process->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_POINT);
	splash_process->set_direction(Vector3(0.0, 1.0, 0.0));
	splash_process->set_spread(55.0);
	splash_process->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 0.8);
	splash_process->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 1.6);
	splash_process->set_gravity(Vector3(0.0, -9.8, 0.0));
	splash_process->set_collision_mode(ParticleProcessMaterial::COLLISION_DISABLED);
	rain_process->set_sub_emitter_mode(ParticleProcessMaterial::SUB_EMITTER_AT_COLLISION);
	rain_process->set_sub_emitter_amount_at_collision(2);
	drops->set_sub_emitter(drops->get_path_to(splashes));

	// Snow drifts down slowly, swirling, carried along by the wind.
	flakes = _make_particles(this, Vector3(26.0, 0.5, 26.0), 9.0, Size2(0.035, 0.035), Color(1.0, 1.0, 1.0, 0.9), false);
	flakes->set_amount(snow_amount);
	Ref<ParticleProcessMaterial> snow_process = flakes->get_process_material();
	snow_process->set_spread(15.0);
	snow_process->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 0.8);
	snow_process->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 1.4);
	snow_process->set_gravity(Vector3(0.0, -0.6, 0.0));
	snow_process->set_turbulence_enabled(true);
	snow_process->set_turbulence_noise_strength(1.5);
	snow_process->set_turbulence_noise_scale(4.0);
	snow_process->set_param_min(ParticleProcessMaterial::PARAM_TURB_VEL_INFLUENCE, 0.05);
	snow_process->set_param_max(ParticleProcessMaterial::PARAM_TURB_VEL_INFLUENCE, 0.12);
	snow_process->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 0.6);
	snow_process->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 1.4);

	// What's overhead round the camera, rendered from above when it moves.
	shelter = memnew(GPUParticlesCollisionHeightField3D);
	shelter->set_size(SHELTER_SIZE);
	shelter->set_resolution(GPUParticlesCollisionHeightField3D::RESOLUTION_512);
	shelter->set_update_mode(GPUParticlesCollisionHeightField3D::UPDATE_MODE_WHEN_MOVED);
	shelter->set_visible(false);
	add_child(shelter, false, INTERNAL_MODE_BACK);

	// A lightning flash's light: brief, unshadowed and from high up.
	flash_light = memnew(DirectionalLight3D);
	flash_light->set_color(Color(0.8, 0.85, 1.0));
	flash_light->set_shadow(false);
	flash_light->set_visible(false);
	add_child(flash_light, false, INTERNAL_MODE_BACK);
}

// Drops left where they were when the world shifts: started afresh round the camera.
void Weather3D::_restart_drops(const Vector3 &p_by) {
	if (drops != nullptr && drops->is_emitting()) {
		drops->restart();
	}
	if (flakes != nullptr && flakes->is_emitting()) {
		flakes->restart();
	}
}

void Weather3D::_step(float p_delta) {
	const bool falling = rain > 0.0f || snow > 0.0f;
	const float wanted_cover = falling ? MAX(cloud_cover, RAIN_CLOUD) : cloud_cover;
	current_cover = Math::move_toward(current_cover, wanted_cover, p_delta / change_time);
	overcast = Math::smoothstep(0.7f, 1.0f, current_cover);
	// Rain and snow start once it's overcast, and stop first.
	current_rain = Math::move_toward(current_rain, overcast > 0.6f ? rain : 0.0f, p_delta * 2.0f / change_time);
	current_snow = Math::move_toward(current_snow, overcast > 0.6f ? snow : 0.0f, p_delta * 2.0f / change_time);
	current_fog = Math::move_toward(current_fog, MAX(fog, MAX(current_rain * RAIN_FOG, current_snow * SNOW_FOG)), p_delta / change_time);
	wetness = Math::move_toward(wetness, current_rain, p_delta / (current_rain > wetness ? wetting_time : drying_time));
	snow_cover = Math::move_toward(snow_cover, current_snow, p_delta / (current_snow > snow_cover ? settling_time : melting_time));
	drift += wind * DRIFT_PER_WIND * p_delta;
	_follow_camera();
	if (drops != nullptr) {
		drops->set_emitting(current_rain > 0.05f);
		drops->set_amount_ratio(current_rain);
		Ref<ParticleProcessMaterial> process = drops->get_process_material();
		if (process.is_valid()) {
			// The wind slants the rain.
			process->set_direction(Vector3(wind.x * 0.03, -1.0, wind.y * 0.03).normalized());
		}
	}
	if (flakes != nullptr) {
		flakes->set_emitting(current_snow > 0.05f);
		flakes->set_amount_ratio(current_snow);
		Ref<ParticleProcessMaterial> process = flakes->get_process_material();
		if (process.is_valid()) {
			// Light flakes go with the wind.
			process->set_gravity(Vector3(wind.x * 0.25, -0.6, wind.y * 0.25));
		}
	}
	_step_lightning(p_delta);
	_apply_fog();
	_publish();
}

// The falling weather and the height map of what's overhead go where the camera is.
void Weather3D::_follow_camera() {
	const Camera3D *camera = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
	if (camera == nullptr) {
		return;
	}
	const Transform3D view = camera->get_global_transform();
	const Vector3 ahead = view.origin - view.basis.get_column(Vector3::AXIS_Z) * 8.0;
	if (drops != nullptr) {
		drops->set_global_position(ahead + Vector3(0.0, 12.0, 0.0));
	}
	if (flakes != nullptr) {
		flakes->set_global_position(ahead + Vector3(0.0, 10.0, 0.0));
	}
	if (shelter == nullptr) {
		return;
	}
	// Only while something falls or lies on the ground: it renders the scene from above as it moves.
	const bool needed = current_rain > 0.0f || current_snow > 0.0f || wetness > 0.0f || snow_cover > 0.0f;
	shelter->set_visible(needed);
	if (!needed) {
		WeatherShelter::set(WeatherShelter::Shelter());
		return;
	}
	// In steps, so it's rendered again only every few metres; reaching further up than down.
	const Vector3 at = (view.origin / SHELTER_STEP).floor() * SHELTER_STEP + Vector3(0.0, SHELTER_SIZE.y * 0.25, 0.0);
	if (!shelter->get_global_position().is_equal_approx(at)) {
		shelter->set_global_transform(Transform3D(Basis(), at));
	}
	WeatherShelter::Shelter published;
	published.collision = shelter->get_base();
	published.transform = shelter->get_global_transform();
	published.extents = SHELTER_SIZE * 0.5;
	WeatherShelter::set(published);
}

// In a storm, now and then a flash: three quick pulses, lighting the clouds and the ground.
void Weather3D::_step_lightning(float p_delta) {
	const bool storm = lightning > 0.0f && current_rain > 0.3f && overcast > 0.8f;
	if (!storm) {
		next_flash = 0.0f; // The next storm's first flash comes a while after it starts.
	} else if (next_flash <= 0.0f) {
		// The more lightning, the sooner: about every 30 seconds at 0.2, every 6 at 1.
		next_flash = random->randf_range(0.5f, 1.5f) * 6.0f / lightning;
	} else if (flash_age < 0.0f) {
		next_flash -= p_delta;
		if (next_flash <= 0.0f) {
			flash();
		}
	}
	float level = 0.0f;
	if (flash_age >= 0.0f) {
		flash_age += p_delta;
		const float t = flash_age;
		if (t < 0.07f) {
			level = 1.0f;
		} else if (t < 0.14f) {
			level = 0.15f;
		} else if (t < 0.22f) {
			level = 0.75f;
		} else {
			level = 0.75f * Math::exp(-(t - 0.22f) * 9.0f);
		}
		level *= flash_strength;
		if (t > 1.0f) {
			flash_age = -1.0f;
			level = 0.0f;
		}
	}
	if (flash_light != nullptr) {
		flash_light->set_param(Light3D::PARAM_ENERGY, level * 2.5f);
		flash_light->set_visible(level > 0.001f);
	}
	SkyGlobals::set(SkyGlobals::LIGHTNING, level);
}

void Weather3D::flash() {
	flash_age = 0.0f;
	flash_strength = random->randf_range(0.5f, 1.0f);
	if (flash_light != nullptr) {
		const float around = random->randf_range(0.0f, Math::TAU);
		const float up = random->randf_range(0.9f, 1.4f);
		const Vector3 from = Vector3(Math::cos(around) * Math::cos(up), Math::sin(up), Math::sin(around) * Math::cos(up));
		flash_light->set_global_basis(Basis::looking_at(-from, Vector3(0, 0, 1)));
	}
	// How far away it struck, so the game can time the thunder (sound covers 343 metres a second).
	emit_signal(SNAME("lightning_flashed"), random->randf_range(400.0f, 6000.0f));
}

// The world's fog thickens with the weather's: its volumetric fog if it has that, else its plain fog.
void Weather3D::_apply_fog() {
	const Ref<World3D> world = is_inside_tree() ? get_world_3d() : Ref<World3D>();
	const Ref<Environment> current = world.is_valid() ? world->get_environment() : Ref<Environment>();
	if (current != environment) {
		_restore_fog();
		environment = current;
		if (environment.is_valid()) {
			own_fog_enabled = environment->is_fog_enabled();
			own_fog_density = environment->get_fog_density();
			own_volumetric_fog_density = environment->get_volumetric_fog_density();
		}
	}
	if (environment.is_null()) {
		return;
	}
	if (environment->is_volumetric_fog_enabled()) {
		environment->set_volumetric_fog_density(own_volumetric_fog_density + current_fog * 0.04f);
	} else {
		environment->set_fog_enabled(own_fog_enabled || current_fog > 0.001f);
		environment->set_fog_density((own_fog_enabled ? own_fog_density : 0.0f) + current_fog * 0.012f);
	}
}

void Weather3D::_restore_fog() {
	if (environment.is_valid()) {
		environment->set_fog_enabled(own_fog_enabled);
		environment->set_fog_density(own_fog_density);
		environment->set_volumetric_fog_density(own_volumetric_fog_density);
		environment.unref();
	}
}

void Weather3D::_publish() {
	SkyGlobals::set(SkyGlobals::CLOUD_COVER, current_cover);
	SkyGlobals::set(SkyGlobals::OVERCAST, overcast);
	SkyGlobals::set(SkyGlobals::RAIN, current_rain);
	SkyGlobals::set(SkyGlobals::WETNESS, wetness);
	SkyGlobals::set(SkyGlobals::SNOW, current_snow);
	SkyGlobals::set(SkyGlobals::SNOW_COVER, snow_cover);
	SkyGlobals::set(SkyGlobals::CLOUD_DRIFT, drift);
	SkyGlobals::set(SkyGlobals::WIND, wind);
	TimeOfDay *clock = nullptr;
	if (!time_of_day.is_empty()) {
		clock = Object::cast_to<TimeOfDay>(get_node_or_null(time_of_day));
	} else if (get_parent() != nullptr) {
		for (int i = 0; i < get_parent()->get_child_count() && clock == nullptr; i++) {
			clock = Object::cast_to<TimeOfDay>(get_parent()->get_child(i));
		}
	}
	if (clock != nullptr) {
		clock->set_overcast(overcast);
	}
}

void Weather3D::settle() {
	const bool falling = rain > 0.0f || snow > 0.0f;
	current_cover = falling ? MAX(cloud_cover, RAIN_CLOUD) : cloud_cover;
	overcast = Math::smoothstep(0.7f, 1.0f, current_cover);
	current_rain = overcast > 0.6f ? rain : 0.0f;
	current_snow = overcast > 0.6f ? snow : 0.0f;
	current_fog = MAX(fog, MAX(current_rain * RAIN_FOG, current_snow * SNOW_FOG));
	wetness = current_rain;
	snow_cover = current_snow;
	if (is_inside_tree()) {
		if (!Engine::get_singleton()->is_editor_hint()) {
			_apply_fog();
		}
		_publish();
	}
}

void Weather3D::set_cloud_cover(float p_cover) {
	cloud_cover = CLAMP(p_cover, 0.0f, 1.0f);
}

float Weather3D::get_cloud_cover() const {
	return cloud_cover;
}

void Weather3D::set_rain(float p_rain) {
	rain = CLAMP(p_rain, 0.0f, 1.0f);
}

float Weather3D::get_rain() const {
	return rain;
}

void Weather3D::set_wind(const Vector2 &p_wind) {
	wind = p_wind;
}

Vector2 Weather3D::get_wind() const {
	return wind;
}

void Weather3D::set_change_time(float p_seconds) {
	change_time = MAX(p_seconds, 0.01f);
}

float Weather3D::get_change_time() const {
	return change_time;
}

void Weather3D::set_wetting_time(float p_seconds) {
	wetting_time = MAX(p_seconds, 0.01f);
}

float Weather3D::get_wetting_time() const {
	return wetting_time;
}

void Weather3D::set_drying_time(float p_seconds) {
	drying_time = MAX(p_seconds, 0.01f);
}

float Weather3D::get_drying_time() const {
	return drying_time;
}

void Weather3D::set_rain_amount(int p_amount) {
	rain_amount = MAX(p_amount, 1);
	if (drops != nullptr) {
		drops->set_amount(rain_amount);
	}
}

int Weather3D::get_rain_amount() const {
	return rain_amount;
}

void Weather3D::set_snow(float p_snow) {
	snow = CLAMP(p_snow, 0.0f, 1.0f);
}

float Weather3D::get_snow() const {
	return snow;
}

void Weather3D::set_snow_amount(int p_amount) {
	snow_amount = MAX(p_amount, 1);
	if (flakes != nullptr) {
		flakes->set_amount(snow_amount);
	}
}

int Weather3D::get_snow_amount() const {
	return snow_amount;
}

void Weather3D::set_settling_time(float p_seconds) {
	settling_time = MAX(p_seconds, 0.01f);
}

float Weather3D::get_settling_time() const {
	return settling_time;
}

void Weather3D::set_melting_time(float p_seconds) {
	melting_time = MAX(p_seconds, 0.01f);
}

float Weather3D::get_melting_time() const {
	return melting_time;
}

void Weather3D::set_fog(float p_fog) {
	fog = CLAMP(p_fog, 0.0f, 1.0f);
}

float Weather3D::get_fog() const {
	return fog;
}

void Weather3D::set_lightning(float p_lightning) {
	lightning = CLAMP(p_lightning, 0.0f, 1.0f);
}

float Weather3D::get_lightning() const {
	return lightning;
}

void Weather3D::set_time_of_day(const NodePath &p_path) {
	time_of_day = p_path;
}

NodePath Weather3D::get_time_of_day() const {
	return time_of_day;
}

float Weather3D::get_current_cloud_cover() const {
	return current_cover;
}

float Weather3D::get_current_rain() const {
	return current_rain;
}

float Weather3D::get_current_snow() const {
	return current_snow;
}

float Weather3D::get_current_fog() const {
	return current_fog;
}

float Weather3D::get_snow_cover() const {
	return snow_cover;
}

float Weather3D::get_overcast() const {
	return overcast;
}

float Weather3D::get_wetness() const {
	return wetness;
}

void Weather3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_cloud_cover", "cover"), &Weather3D::set_cloud_cover);
	ClassDB::bind_method(D_METHOD("get_cloud_cover"), &Weather3D::get_cloud_cover);
	ClassDB::bind_method(D_METHOD("set_rain", "rain"), &Weather3D::set_rain);
	ClassDB::bind_method(D_METHOD("get_rain"), &Weather3D::get_rain);
	ClassDB::bind_method(D_METHOD("set_wind", "wind"), &Weather3D::set_wind);
	ClassDB::bind_method(D_METHOD("get_wind"), &Weather3D::get_wind);
	ClassDB::bind_method(D_METHOD("set_change_time", "seconds"), &Weather3D::set_change_time);
	ClassDB::bind_method(D_METHOD("get_change_time"), &Weather3D::get_change_time);
	ClassDB::bind_method(D_METHOD("set_wetting_time", "seconds"), &Weather3D::set_wetting_time);
	ClassDB::bind_method(D_METHOD("get_wetting_time"), &Weather3D::get_wetting_time);
	ClassDB::bind_method(D_METHOD("set_drying_time", "seconds"), &Weather3D::set_drying_time);
	ClassDB::bind_method(D_METHOD("get_drying_time"), &Weather3D::get_drying_time);
	ClassDB::bind_method(D_METHOD("set_rain_amount", "amount"), &Weather3D::set_rain_amount);
	ClassDB::bind_method(D_METHOD("get_rain_amount"), &Weather3D::get_rain_amount);
	ClassDB::bind_method(D_METHOD("set_snow", "snow"), &Weather3D::set_snow);
	ClassDB::bind_method(D_METHOD("get_snow"), &Weather3D::get_snow);
	ClassDB::bind_method(D_METHOD("set_snow_amount", "amount"), &Weather3D::set_snow_amount);
	ClassDB::bind_method(D_METHOD("get_snow_amount"), &Weather3D::get_snow_amount);
	ClassDB::bind_method(D_METHOD("set_settling_time", "seconds"), &Weather3D::set_settling_time);
	ClassDB::bind_method(D_METHOD("get_settling_time"), &Weather3D::get_settling_time);
	ClassDB::bind_method(D_METHOD("set_melting_time", "seconds"), &Weather3D::set_melting_time);
	ClassDB::bind_method(D_METHOD("get_melting_time"), &Weather3D::get_melting_time);
	ClassDB::bind_method(D_METHOD("set_fog", "fog"), &Weather3D::set_fog);
	ClassDB::bind_method(D_METHOD("get_fog"), &Weather3D::get_fog);
	ClassDB::bind_method(D_METHOD("set_lightning", "lightning"), &Weather3D::set_lightning);
	ClassDB::bind_method(D_METHOD("get_lightning"), &Weather3D::get_lightning);
	ClassDB::bind_method(D_METHOD("set_time_of_day", "path"), &Weather3D::set_time_of_day);
	ClassDB::bind_method(D_METHOD("get_time_of_day"), &Weather3D::get_time_of_day);
	ClassDB::bind_method(D_METHOD("get_current_cloud_cover"), &Weather3D::get_current_cloud_cover);
	ClassDB::bind_method(D_METHOD("get_current_rain"), &Weather3D::get_current_rain);
	ClassDB::bind_method(D_METHOD("get_overcast"), &Weather3D::get_overcast);
	ClassDB::bind_method(D_METHOD("get_current_snow"), &Weather3D::get_current_snow);
	ClassDB::bind_method(D_METHOD("get_current_fog"), &Weather3D::get_current_fog);
	ClassDB::bind_method(D_METHOD("get_wetness"), &Weather3D::get_wetness);
	ClassDB::bind_method(D_METHOD("get_snow_cover"), &Weather3D::get_snow_cover);
	ClassDB::bind_method(D_METHOD("settle"), &Weather3D::settle);
	ClassDB::bind_method(D_METHOD("flash"), &Weather3D::flash);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cloud_cover", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_cloud_cover", "get_cloud_cover");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rain", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_rain", "get_rain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "snow", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_snow", "get_snow");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fog", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_fog", "get_fog");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lightning", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_lightning", "get_lightning");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "wind", PROPERTY_HINT_NONE, "suffix:m/s"), "set_wind", "get_wind");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "change_time", PROPERTY_HINT_RANGE, "0.01,600,0.01,or_greater,suffix:s"), "set_change_time", "get_change_time");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wetting_time", PROPERTY_HINT_RANGE, "0.01,600,0.01,or_greater,suffix:s"), "set_wetting_time", "get_wetting_time");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "drying_time", PROPERTY_HINT_RANGE, "0.01,1800,0.01,or_greater,suffix:s"), "set_drying_time", "get_drying_time");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "settling_time", PROPERTY_HINT_RANGE, "0.01,1800,0.01,or_greater,suffix:s"), "set_settling_time", "get_settling_time");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "melting_time", PROPERTY_HINT_RANGE, "0.01,3600,0.01,or_greater,suffix:s"), "set_melting_time", "get_melting_time");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "rain_amount", PROPERTY_HINT_RANGE, "1,50000,1"), "set_rain_amount", "get_rain_amount");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "snow_amount", PROPERTY_HINT_RANGE, "1,50000,1"), "set_snow_amount", "get_snow_amount");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "time_of_day", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "TimeOfDay"), "set_time_of_day", "get_time_of_day");

	ADD_SIGNAL(MethodInfo("lightning_flashed", PropertyInfo(Variant::FLOAT, "distance")));
}
