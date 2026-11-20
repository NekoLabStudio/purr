#[compute]

#version 450

#VERSION_DEFINES

// Film grain: fine noise that changes every frame, strongest in the mid and dark tones, as on film.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba16f, set = 0, binding = 0) uniform restrict image2D colour_image;

layout(push_constant, std430) uniform Params {
	vec2 size;
	float amount;
	float grain_size; // In pixels.
	float seed; // Changes each frame.
	float coloured; // 0 for the same noise in each channel, 1 for each its own.
	float pad0;
	float pad1;
}
params;

float hash(vec3 p) {
	p = fract(p * vec3(443.897, 441.423, 437.195));
	p += dot(p, p.yzx + 19.19);
	return fract((p.x + p.y) * p.z);
}

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (pixel.x >= int(params.size.x) || pixel.y >= int(params.size.y)) {
		return;
	}
	vec2 cell = floor(vec2(pixel) / max(params.grain_size, 1.0));
	vec3 noise = vec3(hash(vec3(cell, params.seed)));
	if (params.coloured > 0.5) {
		noise.g = hash(vec3(cell, params.seed + 17.0));
		noise.b = hash(vec3(cell, params.seed + 31.0));
	}
	noise -= 0.5;
	vec4 colour = imageLoad(colour_image, pixel);
	float luminance = dot(colour.rgb, vec3(0.2126, 0.7152, 0.0722));
	// Grain shows most in the mid tones; in linear light, scaled with the light so it stays even.
	float weight = params.amount * (1.0 - smoothstep(0.2, 2.0, luminance)) * max(luminance, 0.02) * 4.0;
	colour.rgb = max(colour.rgb + noise * weight, vec3(0.0));
	imageStore(colour_image, pixel, colour);
}
