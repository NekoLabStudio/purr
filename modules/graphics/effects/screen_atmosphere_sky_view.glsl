#[compute]

#version 450

#VERSION_DEFINES

// The sky as seen from the ground (AtmosphereTables, Hillaire 5.3): its
// light by direction round the sun, which AtmosphereSkyMaterial reads. Made again as the sun, the haze or the
// light on the air move on: the camera's height  changes it by
// nothing worth drawing, so it isn't made for each view or each frame.

#include "atmosphere_inc.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba16f, set = 0, binding = 1) uniform restrict writeonly image2D table;
layout(set = 0, binding = 2) uniform sampler2D transmittance_table;
layout(set = 0, binding = 3) uniform sampler2D multi_scattering_table;

#include "atmosphere_march_inc.glsl"

const int STEPS = 32;

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(table);
	if (pixel.x >= size.x || pixel.y >= size.y) {
		return;
	}
	vec2 uv = from_texel_middles((vec2(pixel) + 0.5) / vec2(size), vec2(size));
	float view_r = bottom_radius() + atmosphere.radii.z;
	float view_zenith_cos;
	float light_view_cos;
	sky_view_angles(uv, view_r, view_zenith_cos, light_view_cos);
	float sun_zenith_cos = clamp(atmosphere.sun.y, -1.0, 1.0);
	vec3 to_sun = vec3(sqrt(max(1.0 - sun_zenith_cos * sun_zenith_cos, 0.0)), sun_zenith_cos, 0.0);
	float view_zenith_sin = sqrt(max(1.0 - view_zenith_cos * view_zenith_cos, 0.0));
	vec3 d = vec3(view_zenith_sin * light_view_cos, view_zenith_cos, view_zenith_sin * sqrt(max(1.0 - light_view_cos * light_view_cos, 0.0)));
	March result = march(vec3(0.0, view_r, 0.0), d, to_sun, 0.0, STEPS);
	imageStore(table, pixel, vec4(result.light, 1.0));
}
