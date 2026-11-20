#[compute]

#version 450

#VERSION_DEFINES

// Motion blur: each pixel smeared along how far it moved on screen since the last frame (the
// renderer's motion vectors), over the shutter's share of the frame.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba16f, set = 0, binding = 0) uniform restrict writeonly image2D colour_image;
layout(set = 0, binding = 1) uniform sampler2D source; // The colour as drawn.
layout(set = 0, binding = 2) uniform sampler2D velocity;

layout(push_constant, std430) uniform Params {
	vec2 size;
	float shutter; // The share of the frame's motion smeared.
	float samples;
	float longest; // The longest smear, as a share of the screen's width.
	float pad0;
	float pad1;
	float pad2;
}
params;

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (pixel.x >= int(params.size.x) || pixel.y >= int(params.size.y)) {
		return;
	}
	vec2 uv = (vec2(pixel) + 0.5) / params.size;
	vec2 moved = textureLod(velocity, uv, 0.0).xy * params.shutter;
	float length_moved = length(moved);
	if (length_moved * params.size.x < 1.0) {
		return; // Under a pixel: nothing to smear.
	}
	moved *= min(1.0, params.longest / length_moved);
	vec4 total = vec4(0.0);
	int count = int(params.samples);
	for (int k = 0; k < count; k++) {
		float t = (float(k) + 0.5) / float(count) - 0.5;
		total += textureLod(source, uv - moved * t, 0.0);
	}
	imageStore(colour_image, pixel, total / float(count));
}
