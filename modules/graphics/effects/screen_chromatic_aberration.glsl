#[compute]

#version 450

#VERSION_DEFINES

// Chromatic aberration: red and blue spread apart towards the edges of the picture, as a lens
// bends each colour a little differently.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba16f, set = 0, binding = 0) uniform restrict writeonly image2D colour_image;
layout(set = 0, binding = 1) uniform sampler2D source; // The colour as drawn.

layout(push_constant, std430) uniform Params {
	vec2 size;
	float strength; // How far apart the colours spread at the corners, as a share of the screen.
	float pad;
}
params;

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (pixel.x >= int(params.size.x) || pixel.y >= int(params.size.y)) {
		return;
	}
	vec2 uv = (vec2(pixel) + 0.5) / params.size;
	vec2 from_centre = uv - 0.5;
	vec2 shift = from_centre * dot(from_centre, from_centre) * 2.0 * params.strength;
	vec4 colour = textureLod(source, uv, 0.0);
	colour.r = textureLod(source, uv - shift, 0.0).r;
	colour.b = textureLod(source, uv + shift, 0.0).b;
	imageStore(colour_image, pixel, colour);
}
