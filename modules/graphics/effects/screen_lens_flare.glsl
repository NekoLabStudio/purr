#[compute]

#version 450

#VERSION_DEFINES

// Lens flare: the sun's light bouncing inside the camera's lens, as a glare round the sun and
// coloured ghosts along the line from it through the screen's centre, with a halo. Each pixel
// looks at how much of the sun is open sky (the depth buffer's far plane), so the flare fades as
// the sun goes behind something. Added before tonemapping.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba16f, set = 0, binding = 0) uniform restrict image2D colour_image;
layout(set = 0, binding = 1) uniform sampler2D depth_texture;

layout(push_constant, std430) uniform Params {
	vec2 size; // Pixels.
	vec2 sun; // The sun on screen (0 to 1); may be a little off it.
	vec4 tint; // The flare's colour times its strength.
	float aspect;
	float glare; // The glare's size round the sun (screen heights).
	float ghosts; // The ghosts' and halo's strength against the glare.
	float pad;
}
params;

const int GHOSTS = 5;
const float GHOST_PLACES[GHOSTS] = float[](0.35, 0.7, 1.15, 1.45, 1.8);
const float GHOST_SIZES[GHOSTS] = float[](0.05, 0.03, 0.08, 0.025, 0.12);
const vec3 GHOST_TINTS[GHOSTS] = vec3[](vec3(1.0, 0.75, 0.45), vec3(0.5, 0.8, 1.0), vec3(0.7, 1.0, 0.6), vec3(1.0, 0.5, 0.7), vec3(0.45, 0.6, 1.0));

// The share of the sun's disc that's open sky.
float sun_seen() {
	float open = 0.0;
	for (int y = -2; y <= 2; y++) {
		for (int x = -2; x <= 2; x++) {
			vec2 at = params.sun + vec2(x, y) * 0.004 * vec2(1.0 / params.aspect, 1.0);
			if (all(greaterThanEqual(at, vec2(0.0))) && all(lessThanEqual(at, vec2(1.0)))) {
				open += step(textureLod(depth_texture, at, 0.0).r, 0.0);
			}
		}
	}
	return open / 25.0;
}

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (pixel.x >= int(params.size.x) || pixel.y >= int(params.size.y)) {
		return;
	}
	float seen = sun_seen();
	if (seen <= 0.0) {
		return;
	}
	vec2 uv = (vec2(pixel) + 0.5) / params.size;
	vec2 to_pixel = (uv - params.sun) * vec2(params.aspect, 1.0);
	vec3 light = vec3(0.0);
	// The glare: bright at the sun, falling off quickly.
	float d = length(to_pixel) / max(params.glare, 1e-4);
	light += vec3(1.0) * exp(-d * d * 4.0) * 0.6;
	// The ghosts, mirrored through the centre, and the halo round it.
	vec2 across = vec2(0.5) - params.sun;
	for (int k = 0; k < GHOSTS; k++) {
		vec2 ghost = params.sun + across * GHOST_PLACES[k];
		float r = length((uv - ghost) * vec2(params.aspect, 1.0)) / GHOST_SIZES[k];
		light += GHOST_TINTS[k] * smoothstep(1.0, 0.6, r) * 0.08 * params.ghosts;
	}
	vec2 from_centre = (uv - 0.5) * vec2(params.aspect, 1.0);
	float halo = abs(length(from_centre) - 0.45);
	float facing = max(dot(normalize(from_centre + 1e-5), normalize(-across * vec2(params.aspect, 1.0) + 1e-5)), 0.0);
	light += vec3(0.8, 0.9, 1.0) * smoothstep(0.03, 0.0, halo) * facing * facing * 0.05 * params.ghosts;
	// Fading as the sun leaves the screen.
	vec2 off = max(abs(params.sun - 0.5) - 0.5, 0.0);
	float on_screen = 1.0 - smoothstep(0.0, 0.15, length(off));
	vec4 colour = imageLoad(colour_image, pixel);
	colour.rgb += params.tint.rgb * light * seen * on_screen;
	imageStore(colour_image, pixel, colour);
}
