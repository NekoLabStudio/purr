#include "weather_shelter.h"

#include "core/math/projection.h"
#include "core/object/callable_mp.h"
#include "core/os/mutex.h"
#include "servers/rendering/rendering_server.h"

#include "sky_globals.h"

#ifdef SCREEN_EFFECTS_ENABLED
#include "effects/weather_shelter_texture.h"
#endif

namespace WeatherShelter {

static Mutex mutex;
static Shelter current;

void set(const Shelter &p_shelter) {
	{
		MutexLock lock(mutex);
		current = p_shelter;
	}
	const bool ready = p_shelter.collision.is_valid() && p_shelter.extents.x > 0.0f && p_shelter.extents.y > 0.0f;
#ifdef SCREEN_EFFECTS_ENABLED
	if (ready && RenderingServer::get_singleton() != nullptr) {
		// A world position into the box's 0 to 1 cube, as the heightfield's depth reads.
		const Vector3 size = p_shelter.extents * 2.0f;
		const Transform3D into = Transform3D(Basis::from_scale(Vector3(1.0f, 1.0f, 1.0f) / size), Vector3(0.5f, 0.5f, 0.5f)) * p_shelter.transform.affine_inverse();
		SkyGlobals::set(SkyGlobals::SHELTER_TRANSFORM, Projection(into));
		SkyGlobals::set(SkyGlobals::SHELTER_MARGIN, 0.25f / size.y);
		RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&WeatherShelterTexture::copy).bind(p_shelter.collision));
	}
	SkyGlobals::set(SkyGlobals::SHELTER_READY, ready && WeatherShelterTexture::has_texture() ? 1.0f : 0.0f);
#else
	SkyGlobals::set(SkyGlobals::SHELTER_READY, 0.0f);
#endif
}

Shelter get() {
	MutexLock lock(mutex);
	return current;
}

} // namespace WeatherShelter
