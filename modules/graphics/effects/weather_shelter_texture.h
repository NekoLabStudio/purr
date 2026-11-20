#pragma once

// The weather's height map of what's overhead, as a texture shaders can sample: WeatherShelter asks
// for it to be copied from the particle collider's heightfield each frame, on the render thread,
// and it's the weather_shelter shader global from the first copy on.

#include "core/templates/rid.h"

namespace WeatherShelterTexture {

// On the render thread.
void copy(RID p_collision);
// Whether there's been a copy for shaders to sample.
bool has_texture();
void cleanup();

} // namespace WeatherShelterTexture
