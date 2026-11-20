#[compute]

#version 450

#VERSION_DEFINES

// The weather's height map of what's overhead (a particle collider's heightfield, a depth texture)
// copied into a float texture shaders can sample as the weather_shelter global.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D heights;
layout(r32f, set = 0, binding = 1) uniform restrict writeonly image2D shelter;

layout(push_constant, std430) uniform Params {
	ivec2 size;
	ivec2 pad;
}
params;

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (pixel.x >= params.size.x || pixel.y >= params.size.y) {
		return;
	}
	imageStore(shelter, pixel, vec4(texelFetch(heights, pixel, 0).r, 0.0, 0.0, 0.0));
}
