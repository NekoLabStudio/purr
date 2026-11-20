#[compute]

#version 450

#VERSION_DEFINES

// The air between the camera and what it sees (AerialPerspectiveEffect,
// Hillaire 5.4): for a grid over the view, slice by slice out to its reach,
// the light the air sends towards the camera and how much of what lies
// beyond gets through. Worked out from the view alone, with nothing kept
// from frame to frame, so a headset's two eyes see the same air.

#include "atmosphere_inc.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba16f, set = 0, binding = 1) uniform restrict writeonly image3D volume;
layout(set = 0, binding = 2) uniform sampler2D transmittance_table;
layout(set = 0, binding = 3) uniform sampler2D multi_scattering_table;

#include "atmosphere_march_inc.glsl"

layout(push_constant, std430) uniform Params {
	// The view's corners' directions in the world (y up): top left, top right, bottom left, bottom right.
	vec4 corners[4];
	vec4 reach;  // x km to the last slice, y the camera's height over the ground (km)
} params;

const int SUBSTEPS = 2;

void main() {
	ivec3 size = imageSize(volume);
	ivec2 cell = ivec2(gl_GlobalInvocationID.xy);
	if (cell.x >= size.x || cell.y >= size.y) {
		return;
	}
	vec2 uv = (vec2(cell) + 0.5) / vec2(size.xy);
	vec3 ray = mix(mix(params.corners[0].xyz, params.corners[1].xyz, uv.x), mix(params.corners[2].xyz, params.corners[3].xyz, uv.x), uv.y);
	vec3 d = ray / max(length(ray), 1e-6);
	vec3 o = vec3(0.0, bottom_radius() + params.reach.y, 0.0);
	vec3 to_sun = atmosphere.sun.xyz;
	float cos_theta = dot(d, to_sun);
	float phase_r = rayleigh_phase(cos_theta);
	float phase_m = mie_phase(cos_theta, atmosphere.mie.w);
	vec3 light = vec3(0.0);
	vec3 transmittance = vec3(1.0);
	float slice_length = params.reach.x / float(size.z);
	float dt = slice_length / float(SUBSTEPS);
	for (int k = 0; k < size.z; k++) {
		for (int s = 0; s < SUBSTEPS; s++) {
			vec3 p = o + d * (float(k) * slice_length + (float(s) + 0.5) * dt);
			// Never under the ground the planet's sphere has: down a hill the ground lies lower than it.
			float height = max(length(p) - bottom_radius(), 0.0);
			Medium m = medium_at(height);
			vec3 extinction = max(m.extinction, vec3(1e-7));
			vec3 step_transmittance = exp(-extinction * dt);
			vec3 at = p / max(length(p), 1e-3) * (bottom_radius() + height);
			vec3 scattered = in_scattered(at, to_sun, m, phase_r, phase_m);
			light += transmittance * (scattered - scattered * step_transmittance) / extinction;
			transmittance *= step_transmittance;
		}
		imageStore(volume, ivec3(cell, k), vec4(light, dot(transmittance, vec3(1.0 / 3.0))));
	}
}
