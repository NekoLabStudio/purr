#[compute]

#version 450

#VERSION_DEFINES

// The atmosphere's transmittance table (AtmosphereTables): how much
// light gets through from a height along a zenith angle to the top of the
// atmosphere (or the ground), made again only when its figures change.

#include "atmosphere_inc.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba16f, set = 0, binding = 1) uniform restrict writeonly image2D table;

const int STEPS = 40;

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = imageSize(table);
	if (pixel.x >= size.x || pixel.y >= size.y) {
		return;
	}
	vec2 uv = from_texel_middles((vec2(pixel) + 0.5) / vec2(size), vec2(size));
	float r;
	float mu;
	transmittance_r_mu(uv, r, mu);
	vec3 o = vec3(0.0, r, 0.0);
	vec3 d = vec3(sqrt(max(1.0 - mu * mu, 0.0)), mu, 0.0);
	float t_max = ray_sphere(o, d, top_radius());
	float to_ground = ray_sphere(o, d, bottom_radius());
	if (to_ground > 0.0 && (t_max < 0.0 || to_ground < t_max)) {
		t_max = to_ground;
	}
	t_max = max(t_max, 0.0);
	vec3 depth = vec3(0.0);
	for (int i = 0; i < STEPS; i++) {
		float t;
		float dt;
		ray_step(i, STEPS, t_max, t, dt);
		vec3 p = o + d * t;
		depth += medium_at(length(p) - bottom_radius()).extinction * dt;
	}
	imageStore(table, pixel, vec4(exp(-depth), 1.0));
}
