#pragma once

// Where the sky is open: Weather3D's height map of what's overhead round the camera (a particle
// collider's heightfield, which also stops its rain at roofs), for the effects that wet and whiten
// only what rain and snow can reach. Set on the main thread, read on the render thread. Shaders
// get it as the weather_shelter globals: the heightfield copied each frame into a texture of
// their own (with the RenderingDevice renderers).

#include "core/math/transform_3d.h"
#include "core/templates/rid.h"

namespace WeatherShelter {

struct Shelter {
	RID collision; // The particles collision whose heightfield it is; empty while there's none.
	Transform3D transform; // The heightfield's box, centred on its origin.
	Vector3 extents;
};

void set(const Shelter &p_shelter);
Shelter get();

} // namespace WeatherShelter
