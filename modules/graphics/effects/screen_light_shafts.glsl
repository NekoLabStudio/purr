#[compute]

#version 450

#VERSION_DEFINES

// Light shafts: from each pixel, a march towards the sun on screen counting the open sky it
// passes (the depth buffer's far plane), each step weighing less, so gaps between buildings and
// leaves throw rays of light out over what stands in front. Added before tonemapping.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba16f, set = 0, binding = 0) uniform restrict image2D colour_image;
layout(set = 0, binding = 1) uniform sampler2D depth_texture;

layout(push_constant, std430) uniform Params {
	vec2 size; // Pixels.
	vec2 sun; // The sun on screen (0 to 1).
	vec4 tint; // rgb: the shafts' colour and strength; a: how much each step weighs after the last.
	float samples;
	float reach; // The share of the way to the sun marched.
	float aspect;
	float spread; // How far from the sun on screen the shafts reach (screen heights).
}
params;

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (pixel.x >= int(params.size.x) || pixel.y >= int(params.size.y)) {
		return;
	}
	vec2 uv = (vec2(pixel) + 0.5) / params.size;
	vec2 off_sun = (uv - params.sun) * vec2(params.aspect, 1.0);
	float near_sun = 1.0 - smoothstep(0.0, params.spread, length(off_sun));
	if (near_sun <= 0.0) {
		return;
	}
	vec2 to_sun = (params.sun - uv) * params.reach / params.samples;
	float light = 0.0;
	float weight = 1.0;
	float total = 0.0;
	vec2 at = uv;
	for (int k = 0; k < int(params.samples); k++) {
		at += to_sun;
		total += weight;
		if (all(greaterThanEqual(at, vec2(0.0))) && all(lessThanEqual(at, vec2(1.0)))) {
			// Depth runs from 1 near to 0 at the far plane, where the sky is.
			light += step(textureLod(depth_texture, at, 0.0).r, 0.0) * weight;
		}
		weight *= params.tint.a;
	}
	light /= max(total, 1e-3);
	// Over the open sky itself only a little: there the sun's own glow says it.
	float own_sky = step(textureLod(depth_texture, uv, 0.0).r, 0.0);
	vec4 colour = imageLoad(colour_image, pixel);
	colour.rgb += params.tint.rgb * light * near_sun * mix(1.0, 0.3, own_sky);
	imageStore(colour_image, pixel, colour);
}
