#[compute]

#version 450

#VERSION_DEFINES

// Vignette: the picture darkening towards its corners, as through a real lens.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba16f, set = 0, binding = 0) uniform restrict image2D colour_image;

layout(push_constant, std430) uniform Params {
	vec2 size;
	float strength; // How dark the corners get (0 to 1).
	float softness; // How gradually it darkens (0 to 1).
	vec4 colour; // What the corners darken to.
	float aspect;
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
	// Rounder on wide screens: the distance from the centre in screen heights, scaled to reach 1 at a corner.
	vec2 from_centre = (uv - 0.5) * vec2(params.aspect, 1.0);
	float corner = length(vec2(params.aspect, 1.0) * 0.5);
	float d = length(from_centre) / corner;
	float dark = smoothstep(1.0 - params.softness, 1.0 + params.softness * 0.25, d) * params.strength;
	vec4 colour = imageLoad(colour_image, pixel);
	colour.rgb = mix(colour.rgb, params.colour.rgb, dark);
	imageStore(colour_image, pixel, colour);
}
