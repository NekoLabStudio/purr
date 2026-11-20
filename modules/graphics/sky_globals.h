#pragma once

// The shader globals TimeOfDay and Weather3D set and CloudySkyMaterial (or a game's own shaders)
// read. They're added to the renderer when first needed, so a project needn't declare them; one
// the project declares itself is left as it is.
//   time_of_day_sun_direction  vec3   towards the sun, even below the horizon (zero without a TimeOfDay)
//   time_of_day_moon_direction vec3   towards the moon (zero without a TimeOfDay)
//   time_of_day_star_rotation  mat3   from the stars' own directions to the sky's as it's turned now (zero without one)
//   time_of_day_night          float  0 by day to 1 at night: when lights come on and windows glow
//   weather_cloud_cover        float  0 to 1 (below 0 without a Weather3D)
//   weather_overcast           float  0 to 1: a grey deck over the sun
//   weather_rain               float  0 to 1
//   weather_wetness            float  0 to 1: lags behind the rain, as wet ground does
//   weather_cloud_drift        vec2   how far the wind has carried the clouds
//   weather_wind               vec2   the wind across the ground, metres a second (x east, y south)
//   weather_snow               float  0 to 1: how hard it's snowing
//   weather_snow_cover         float  0 to 1: how much snow lies on open ground, building and melting slowly
//   weather_lightning          float  0 to 1: a lightning flash's light, for skies and clouds
//   weather_shelter            sampler2D  the height map of what's overhead round the camera (WeatherShelter)
//   weather_shelter_transform  mat4   a world position into that map's 0 to 1 box
//   weather_shelter_ready      float  1 while there's a map
//   weather_shelter_margin     float  a quarter of a metre in the map's height

#include "core/variant/variant.h"

namespace SkyGlobals {

inline constexpr const char *SUN_DIRECTION = "time_of_day_sun_direction";
inline constexpr const char *MOON_DIRECTION = "time_of_day_moon_direction";
inline constexpr const char *STAR_ROTATION = "time_of_day_star_rotation";
inline constexpr const char *NIGHT = "time_of_day_night";
inline constexpr const char *CLOUD_COVER = "weather_cloud_cover";
inline constexpr const char *OVERCAST = "weather_overcast";
inline constexpr const char *RAIN = "weather_rain";
inline constexpr const char *WETNESS = "weather_wetness";
inline constexpr const char *CLOUD_DRIFT = "weather_cloud_drift";
inline constexpr const char *WIND = "weather_wind";
inline constexpr const char *SNOW = "weather_snow";
inline constexpr const char *SNOW_COVER = "weather_snow_cover";
inline constexpr const char *LIGHTNING = "weather_lightning";
inline constexpr const char *SHELTER = "weather_shelter";
inline constexpr const char *SHELTER_TRANSFORM = "weather_shelter_transform";
inline constexpr const char *SHELTER_READY = "weather_shelter_ready";
inline constexpr const char *SHELTER_MARGIN = "weather_shelter_margin";

void ensure();
void set(const char *p_name, const Variant &p_value);
// The value last set here, without asking the renderer (which only answers in the editor). From
// any thread.
Variant get(const char *p_name);
// Forgets the values, as the engine shuts down.
void cleanup();

} // namespace SkyGlobals
