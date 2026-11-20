#[compute]

#version 450

#VERSION_DEFINES

// The clouds' noise (AtmosphereSkyMaterial), made once: a tiling 3D
// texture each, the shape's (r: Perlin-Worley, gba: Worley at three
// frequencies) and the detail's (rgb: Worley at three higher ones), as
// Schneider's Nubis builds clouds: the shape's low frequencies give the
// billows, the detail erodes their edges into wisps.

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
layout(rgba8, set = 0, binding = 0) uniform restrict writeonly image3D noise;

layout(push_constant, std430) uniform Params {
	vec4 kind;  // x: 0 the shape's, 1 the detail's; y its base frequency (cells across)
} params;

uint pcg(uint v) {
	uint state = v * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

vec3 hash3(ivec3 c) {
	uint h = pcg(uint(c.x) ^ pcg(uint(c.y) ^ pcg(uint(c.z))));
	return vec3(float(h & 1023u), float((h >> 10u) & 1023u), float((h >> 20u) & 1023u)) / 1023.0;
}

// Worley noise tiling every `cells`: 1 at a feature point, falling to 0 a cell away.
float worley(vec3 p, float cells) {
	vec3 at = p * cells;
	ivec3 cell = ivec3(floor(at));
	float nearest = 1.0;
	for (int x = -1; x <= 1; x++) {
		for (int y = -1; y <= 1; y++) {
			for (int z = -1; z <= 1; z++) {
				ivec3 c = cell + ivec3(x, y, z);
				ivec3 wrapped = ivec3(mod(vec3(c), cells));
				vec3 point = vec3(c) + hash3(wrapped);
				nearest = min(nearest, length(at - point));
			}
		}
	}
	return 1.0 - clamp(nearest, 0.0, 1.0);
}

float worley_fbm(vec3 p, float cells) {
	return worley(p, cells) * 0.625 + worley(p, cells * 2.0) * 0.25 + worley(p, cells * 4.0) * 0.125;
}

// Gradient noise tiling every `cells`.
float gradient(vec3 p, float cells) {
	vec3 at = p * cells;
	vec3 i = floor(at);
	vec3 f = at - i;
	vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	float result = 0.0;
	for (int k = 0; k < 8; k++) {
		vec3 corner = vec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
		ivec3 wrapped = ivec3(mod(i + corner, cells));
		vec3 g = hash3(wrapped) * 2.0 - 1.0;
		float w = mix(1.0 - u.x, u.x, corner.x) * mix(1.0 - u.y, u.y, corner.y) * mix(1.0 - u.z, u.z, corner.z);
		result += w * dot(g, f - corner);
	}
	return result;
}

float perlin_fbm(vec3 p, float cells) {
	float value = 0.0;
	float amplitude = 0.5;
	for (int octave = 0; octave < 5; octave++) {
		value += gradient(p, cells) * amplitude;
		cells *= 2.0;
		amplitude *= 0.5;
	}
	return clamp(value * 0.9 + 0.5, 0.0, 1.0);
}

float remap(float v, float from_low, float from_high, float to_low, float to_high) {
	return to_low + (v - from_low) / max(from_high - from_low, 1e-5) * (to_high - to_low);
}

void main() {
	ivec3 voxel = ivec3(gl_GlobalInvocationID.xyz);
	ivec3 size = imageSize(noise);
	if (any(greaterThanEqual(voxel, size))) {
		return;
	}
	vec3 p = (vec3(voxel) + 0.5) / vec3(size);
	float cells = params.kind.y;
	vec4 value;
	if (params.kind.x < 0.5) {
		float worleys = worley_fbm(p, cells);
		float perlin_worley = clamp(remap(perlin_fbm(p, cells), worleys - 1.0, 1.0, 0.0, 1.0), 0.0, 1.0);
		value = vec4(perlin_worley, worley_fbm(p, cells * 2.0), worley_fbm(p, cells * 4.0), worley_fbm(p, cells * 8.0));
	} else {
		value = vec4(worley_fbm(p, cells), worley_fbm(p, cells * 2.0), worley_fbm(p, cells * 4.0), 1.0);
	}
	imageStore(noise, voxel, value);
}
