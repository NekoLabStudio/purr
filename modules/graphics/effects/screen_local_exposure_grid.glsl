#[compute]

#version 450

#VERSION_DEFINES

// Local exposure's grid (LocalExposureEffect): for each
// cell of CELL by CELL pixels, the mean log luminance of its pixels in each
// of BINS bands of brightness (a bilateral grid), and in one band more the
// cell's own mean, each with the share of the cell's pixels in it.

#define CELL 32
#define BINS 32

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(rgba16f, set = 0, binding = 0) uniform restrict readonly image2D colour_image;
layout(rg16f, set = 0, binding = 1) uniform restrict writeonly image3D grid;

layout(push_constant, std430) uniform Params {
	vec4 range;  // x the darkest log2 luminance a band covers, y the brightest; zw the picture's size in pixels
} params;

shared uint counts[BINS + 1];
shared uint sums[BINS + 1];  // (log2 luminance - range.x) * 256, summed

void main() {
	uint index = gl_LocalInvocationIndex;
	if (index < BINS + 1) {
		counts[index] = 0u;
		sums[index] = 0u;
	}
	barrier();
	ivec2 cell = ivec2(gl_WorkGroupID.xy);
	ivec2 size = ivec2(params.range.zw);
	float span = max(params.range.y - params.range.x, 1e-3);
	// Each thread takes 2 by 2 of the cell's pixels.
	for (int k = 0; k < 4; k++) {
		ivec2 pixel = cell * CELL + ivec2(gl_LocalInvocationID.xy) * 2 + ivec2(k & 1, k >> 1);
		if (pixel.x >= size.x || pixel.y >= size.y) {
			continue;
		}
		vec3 colour = imageLoad(colour_image, pixel).rgb;
		float luminance = max(dot(colour, vec3(0.2126, 0.7152, 0.0722)), 1e-6);
		float level = clamp(log2(luminance), params.range.x, params.range.y);
		uint bin = uint(clamp((level - params.range.x) / span * float(BINS), 0.0, float(BINS) - 1.0));
		uint fixed_point = uint((level - params.range.x) * 256.0);
		atomicAdd(counts[bin], 1u);
		atomicAdd(sums[bin], fixed_point);
		atomicAdd(counts[BINS], 1u);
		atomicAdd(sums[BINS], fixed_point);
	}
	barrier();
	if (index < BINS + 1) {
		float pixels = float(CELL * CELL);
		float count = float(counts[index]);
		float mean = count > 0.0 ? float(sums[index]) / 256.0 / count + params.range.x : 0.0;
		// Premultiplied by the share, so blurring and filtering weigh each band by how many pixels it holds.
		float share = count / pixels;
		imageStore(grid, ivec3(cell, int(index)), vec4(mean * share, share, 0.0, 0.0));
	}
}
