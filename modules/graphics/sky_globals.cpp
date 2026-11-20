#include "sky_globals.h"

#include "core/config/project_settings.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "servers/rendering/rendering_server.h"

namespace SkyGlobals {

static bool ensured = false;
// The main thread's copy. Made and freed with the engine, never a static: Variants can't outlive it.
static HashMap<String, Variant> *values = nullptr;
static Mutex values_mutex; // The render thread reads them too.

void ensure() {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs == nullptr || ensured) {
		return;
	}
	ensured = true;
	{
		MutexLock lock(values_mutex);
		if (values == nullptr) {
			values = memnew((HashMap<String, Variant>));
		}
	}
	struct Global {
		const char *name;
		RSE::GlobalShaderParameterType type;
		Variant value;
	};
	const Global globals[] = {
		{ SUN_DIRECTION, RSE::GLOBAL_VAR_TYPE_VEC3, Vector3() },
		{ MOON_DIRECTION, RSE::GLOBAL_VAR_TYPE_VEC3, Vector3() },
		{ STAR_ROTATION, RSE::GLOBAL_VAR_TYPE_MAT3, Basis(Vector3(), Vector3(), Vector3()) },
		{ NIGHT, RSE::GLOBAL_VAR_TYPE_FLOAT, 0.0 },
		{ CLOUD_COVER, RSE::GLOBAL_VAR_TYPE_FLOAT, -1.0 },
		{ OVERCAST, RSE::GLOBAL_VAR_TYPE_FLOAT, 0.0 },
		{ RAIN, RSE::GLOBAL_VAR_TYPE_FLOAT, 0.0 },
		{ WETNESS, RSE::GLOBAL_VAR_TYPE_FLOAT, 0.0 },
		{ CLOUD_DRIFT, RSE::GLOBAL_VAR_TYPE_VEC2, Vector2() },
		// A light breeze until a Weather3D says otherwise, so foliage isn't stock still.
		{ WIND, RSE::GLOBAL_VAR_TYPE_VEC2, Vector2(2.0, 0.8) },
		{ SNOW, RSE::GLOBAL_VAR_TYPE_FLOAT, 0.0 },
		{ SNOW_COVER, RSE::GLOBAL_VAR_TYPE_FLOAT, 0.0 },
		{ LIGHTNING, RSE::GLOBAL_VAR_TYPE_FLOAT, 0.0 },
		{ SHELTER, RSE::GLOBAL_VAR_TYPE_SAMPLER2D, Variant() },
		{ SHELTER_TRANSFORM, RSE::GLOBAL_VAR_TYPE_MAT4, Projection() },
		{ SHELTER_READY, RSE::GLOBAL_VAR_TYPE_FLOAT, 0.0 },
		{ SHELTER_MARGIN, RSE::GLOBAL_VAR_TYPE_FLOAT, 0.0 },
	};
	for (const Global &global : globals) {
		// The renderer won't list its globals outside the editor: a project's own are its settings.
		if (!ProjectSettings::get_singleton()->has_setting(String("shader_globals/") + global.name)) {
			rs->global_shader_parameter_add(global.name, global.type, global.value);
		}
		MutexLock lock(values_mutex);
		if (!values->has(global.name)) {
			(*values)[global.name] = global.value;
		}
	}
}

void set(const char *p_name, const Variant &p_value) {
	ensure();
	{
		MutexLock lock(values_mutex);
		if (values != nullptr) {
			(*values)[p_name] = p_value;
		}
	}
	if (RenderingServer::get_singleton() != nullptr) {
		RenderingServer::get_singleton()->global_shader_parameter_set(p_name, p_value);
	}
}

void cleanup() {
	MutexLock lock(values_mutex);
	if (values != nullptr) {
		memdelete(values);
		values = nullptr;
	}
	ensured = false;
}

Variant get(const char *p_name) {
	// Not ensure(): it adds globals to the renderer, and this may be on the render thread.
	MutexLock lock(values_mutex);
	const Variant *value = values != nullptr ? values->getptr(p_name) : nullptr;
	return value != nullptr ? *value : Variant();
}

} // namespace SkyGlobals
