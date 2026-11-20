#[compute]

#version 450

#VERSION_DEFINES

// The atmosphere's multiple scattering table (AtmosphereTables,
// Hillaire 5.5): by height and the sun's zenith angle, the light scattered
// twice or more, as the sum of every order from the second (an even light
// gathered from 64 directions, then the series 1 / (1 - f) of what's
// scattered on). Made again only when its figures change.

#define MULTI_SCATTERING_PASS
#include "atmosphere_inc.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba16f, set = 0, binding = 1) uniform restrict writeonly image2D table;
layout(set = 0, binding = 2) uniform sampler2D transmittance_table;

#include "atmosphere_march_inc.glsl"

const int STEPS = 20;
const int SQRT_DIRECTIONS = 8;

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(table);
	if (pixel.x >= size.x || pixel.y >= size.y) {
		return;
	}
	vec2 uv = from_texel_middles((vec2(pixel) + 0.5) / vec2(size), vec2(size));
	float mu = uv.x * 2.0 - 1.0;
	// Just over the ground, so the ground's own sphere isn't met where the ray starts.
	float r = bottom_radius() + max(uv.y * (top_radius() - bottom_radius()), 0.01);
	vec3 o = vec3(0.0, r, 0.0);
	vec3 to_sun = vec3(sqrt(max(1.0 - mu * mu, 0.0)), mu, 0.0);
	vec3 light = vec3(0.0);
	vec3 multi = vec3(0.0);
	for (int i = 0; i < SQRT_DIRECTIONS; i++) {
		for (int j = 0; j < SQRT_DIRECTIONS; j++) {
			float around = 2.0 * PI * (float(i) + 0.5) / float(SQRT_DIRECTIONS);
			float cos_up = 1.0 - 2.0 * (float(j) + 0.5) / float(SQRT_DIRECTIONS);
			float sin_up = sqrt(max(1.0 - cos_up * cos_up, 0.0));
			vec3 d = vec3(cos(around) * sin_up, cos_up, sin(around) * sin_up);
			March result = march(o, d, to_sun, 0.0, STEPS);
			light += result.light;
			multi += result.multi;
		}
	}
	float directions = float(SQRT_DIRECTIONS * SQRT_DIRECTIONS);
	// Averaged over the sphere: the even phase (1 / 4 pi) times the sphere's 4 pi.
	light /= directions;
	multi /= directions;
	vec3 every_order = 1.0 / (1.0 - clamp(multi, vec3(0.0), vec3(0.99)));
	imageStore(table, pixel, vec4(light * every_order, 1.0));
}
