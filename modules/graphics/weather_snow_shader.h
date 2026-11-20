#pragma once

// Snow on the world's surfaces, as shader code shared by every lit StandardMaterial3D (through
// BaseMaterial3D's extra code), terrain and FoliageMaterial: whatever faces up, under open sky,
// whitens as Weather3D's snow cover builds. Open sky is Weather3D's height map of what's overhead
// round the camera (weather_shelter), as WeatherShelter copies it each frame; past it, or without
// one, everything is open.

inline constexpr const char *WEATHER_SNOW_DECLARATIONS = R"(
global uniform float weather_snow_cover;
global uniform sampler2D weather_shelter;
global uniform mat4 weather_shelter_transform; // A world position into the height map's 0 to 1 box.
global uniform float weather_shelter_ready;
global uniform float weather_shelter_margin; // A quarter of a metre, in the box's height.

// Whether the sky's open over a world position: nothing in the height map above it.
float weather_open_sky(vec3 world) {
	if (weather_shelter_ready < 0.5) {
		return 1.0;
	}
	vec3 at = (weather_shelter_transform * vec4(world, 1.0)).xyz;
	if (any(lessThan(at.xz, vec2(0.0))) || any(greaterThan(at.xz, vec2(1.0)))) {
		return 1.0;
	}
	float top = textureLod(weather_shelter, at.xz, 0.0).r;
	return step(top - weather_shelter_margin, at.y);
}

// How much snow lies at a world position facing world_normal, from 0 to 1.
float weather_snow(vec3 world, vec3 world_normal) {
	if (weather_snow_cover <= 0.0) {
		return 0.0;
	}
	float lying = weather_snow_cover * 1.3 - (1.0 - smoothstep(0.55, 0.85, world_normal.y));
	return smoothstep(0.0, 0.15, lying) * weather_open_sky(world + vec3(0.0, 0.05, 0.0));
}
)";

// For BaseMaterial3D: the snow over the material's own albedo and roughness.
inline constexpr const char *WEATHER_SNOW_FRAGMENT = R"(
	{
		float snow = weather_snow((INV_VIEW_MATRIX * vec4(VERTEX, 1.0)).xyz, normalize(mat3(INV_VIEW_MATRIX) * NORMAL));
		ALBEDO = mix(ALBEDO, vec3(0.9, 0.92, 0.95), snow);
		ROUGHNESS = mix(ROUGHNESS, 0.65, snow);
	}
)";
