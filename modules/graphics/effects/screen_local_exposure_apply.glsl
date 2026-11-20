#[compute]

#version 450

#VERSION_DEFINES

// Local exposure (LocalExposureEffect): each pixel's light
// round it read from the blurred grid at its own brightness (the bilateral
// grid's band, so a lit sign's edge doesn't darken the alley beside it),
// mixed with the plain blur of its neighbourhood by detail; where that light
// is brighter than the exposure's middle grey it's brought down by
// highlights, darker brought up by shadows, as contrast scales of the log
// luminance (1 leaves it be). Before tone mapping, so the global exposure and
// auto exposure still set the picture's brightness.

#define BINS 32

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba16f, set = 0, binding = 0) uniform image2D colour_image;
layout(set = 0, binding = 1) uniform sampler3D grid;

layout(push_constant, std430) uniform Params {
	vec4 range;  // x the darkest log2 luminance a band covers, y the brightest; zw the picture's size in pixels
	vec4 shape;  // x middle grey's log2 luminance (before exposure), y highlights' contrast, z shadows' contrast, w detail (0 the plain blur, 1 the bilateral)
	vec4 cells;  // xy the grid's cells across and down, z one cell's pixels across
} params;

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (pixel.x >= int(params.range.z) || pixel.y >= int(params.range.w)) {
		return;
	}
	vec4 colour = imageLoad(colour_image, pixel);
	float luminance = max(dot(colour.rgb, vec3(0.2126, 0.7152, 0.0722)), 1e-6);
	float level = clamp(log2(luminance), params.range.x, params.range.y);
	float span = max(params.range.y - params.range.x, 1e-3);
	vec2 at = (vec2(pixel) + 0.5) / params.cells.z / params.cells.xy;
	float band = clamp((level - params.range.x) / span * float(BINS), 0.5, float(BINS) - 0.5);
	vec2 near = textureLod(grid, vec3(at, band / float(BINS + 1)), 0.0).rg;
	vec2 whole = textureLod(grid, vec3(at, (float(BINS) + 0.5) / float(BINS + 1)), 0.0).rg;
	float blurred = whole.y > 1e-4 ? whole.x / whole.y : level;
	float bilateral = near.y > 1e-4 ? near.x / near.y : blurred;
	float local = mix(blurred, bilateral, params.shape.w);
	float away = local - params.shape.x;
	float contrast = away > 0.0 ? params.shape.y : params.shape.z;
	// The neighbourhood's light moved towards middle grey, the pixel's own detail against it kept.
	float gain = exp2(away * (contrast - 1.0));
	imageStore(colour_image, pixel, vec4(colour.rgb * gain, colour.a));
}
