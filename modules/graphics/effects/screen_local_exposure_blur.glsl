#[compute]

#version 450

#VERSION_DEFINES

// Local exposure's grid blurred across the picture (LocalExposureEffect):
// five cells along one way (x or y) a pass, every band alike, so the light
// round a pixel comes from a wide neighbourhood of like brightness.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rg16f, set = 0, binding = 0) uniform restrict readonly image3D from_grid;
layout(rg16f, set = 0, binding = 1) uniform restrict writeonly image3D to_grid;

layout(push_constant, std430) uniform Params {
	ivec4 way;  // xy the way blurred along, one cell at a time
} params;

void main() {
	ivec3 at = ivec3(gl_GlobalInvocationID.xyz);
	ivec3 size = imageSize(from_grid);
	if (any(greaterThanEqual(at, size))) {
		return;
	}
	const float weights[5] = float[](1.0, 4.0, 6.0, 4.0, 1.0);
	vec2 sum = vec2(0.0);
	float total = 0.0;
	for (int k = -2; k <= 2; k++) {
		ivec3 from = at + ivec3(params.way.xy * k, 0);
		if (any(lessThan(from.xy, ivec2(0))) || any(greaterThanEqual(from.xy, size.xy))) {
			continue;
		}
		sum += imageLoad(from_grid, from).rg * weights[k + 2];
		total += weights[k + 2];
	}
	imageStore(to_grid, at, vec4(sum / max(total, 1e-4), 0.0, 0.0));
}
