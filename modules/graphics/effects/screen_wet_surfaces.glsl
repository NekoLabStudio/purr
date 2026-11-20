#[compute]

#version 450

#VERSION_DEFINES

// Wet surfaces (WetSurfacesEffect), drawn after the sky and before transparent things: what rain
// can reach darkens as it gets wet, takes on a sheen, and on flat ground gathers puddles that
// reflect what's on screen above them, rippling while it rains. What has something overhead in
// the weather's height map stays dry.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba16f, set = 0, binding = 0) uniform restrict writeonly image2D colour_image;
layout(set = 0, binding = 1) uniform sampler2D colour_copy;
layout(set = 0, binding = 2) uniform sampler2D depth_texture;
layout(set = 0, binding = 3) uniform sampler2D normal_roughness;
layout(set = 0, binding = 4) uniform sampler2D shelter_heights;

layout(set = 0, binding = 5, std140) uniform Params {
	mat4 inverse_projection; // The view's, as Godot's depth reads (reversed, depth-corrected).
	mat4 projection;
	mat4 view_to_world;
	mat4 world_to_shelter; // Into the height map's box, centred on its origin.
	vec4 shelter_extents; // xyz half the box; w 1 when there's a height map.
	vec4 weather; // x wetness, y rain, z seconds, w how much of flat ground puddles cover when soaked
	vec4 screen; // xy the picture in pixels, z how dark wet surfaces get, w how strongly they reflect
}
params;

uint pcg(uint v) {
	uint state = v * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

float hash(vec2 p) {
	uvec2 q = uvec2(ivec2(floor(p)) + 32768);
	return float(pcg(q.x ^ pcg(q.y))) * (1.0 / 4294967296.0);
}

float noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

float fbm(vec2 p) {
	float value = 0.0;
	float amplitude = 0.5;
	for (int i = 0; i < 4; i++) {
		value += amplitude * noise(p);
		p = p * 2.03 + vec2(5.2, 1.3);
		amplitude *= 0.5;
	}
	return value;
}

// Rings spreading where drops land, as a tilt of the water's surface: a few drops in each cell of
// a grid, each landing at its own moment.
vec2 ripples(vec2 at, float seconds) {
	vec2 tilt = vec2(0.0);
	for (int layer = 0; layer < 2; layer++) {
		vec2 p = at * (layer == 0 ? 2.3 : 3.1) + float(layer) * 17.0;
		vec2 cell = floor(p);
		for (int y = -1; y <= 1; y++) {
			for (int x = -1; x <= 1; x++) {
				vec2 c = cell + vec2(x, y);
				float h = hash(c);
				vec2 centre = c + vec2(hash(c + 3.1), hash(c + 7.7));
				float age = fract(seconds * 1.3 + h);
				vec2 away = p - centre;
				float d = length(away);
				float ring = d - age * 0.9;
				float wave = sin(ring * 40.0) * exp(-ring * ring * 60.0) * (1.0 - age) * (1.0 - age);
				tilt += away / max(d, 1e-4) * wave;
			}
		}
	}
	return tilt * 0.12;
}

// Something overhead: the height map's top is above this point. Outside the map, open.
bool sheltered(vec3 world) {
	if (params.shelter_extents.w < 0.5) {
		return false;
	}
	vec3 local = (params.world_to_shelter * vec4(world, 1.0)).xyz;
	if (any(greaterThan(abs(local.xz), params.shelter_extents.xz))) {
		return false;
	}
	vec2 uv = local.xz / params.shelter_extents.xz * 0.5 + 0.5;
	float top = (textureLod(shelter_heights, uv, 0.0).r * 2.0 - 1.0) * params.shelter_extents.y;
	return local.y < top - 0.25;
}

vec3 view_position(vec2 uv, float depth) {
	vec4 view = params.inverse_projection * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
	return view.xyz / (abs(view.w) > 1e-8 ? view.w : 1e-8);
}

vec3 on_screen(vec3 view) {
	vec4 clip = params.projection * vec4(view, 1.0);
	clip.xyz /= clip.w;
	return vec3(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5, clip.z);
}

// What's reflected along a ray from a point in view space: the first thing on screen it passes
// behind, or else the last of the screen it saw, which looking down at water is mostly sky.
vec3 reflected(vec3 from, vec3 ray, out float found) {
	found = 0.0;
	float step_length = 0.4;
	vec3 at = from;
	vec2 last_uv = on_screen(from).xy;
	for (int i = 0; i < 32; i++) {
		at += ray * step_length;
		step_length *= 1.12;
		vec3 screen = on_screen(at);
		if (any(lessThan(screen.xy, vec2(0.0))) || any(greaterThan(screen.xy, vec2(1.0))) || at.z > -0.05) {
			break;
		}
		last_uv = screen.xy;
		float scene_depth = textureLod(depth_texture, screen.xy, 0.0).r;
		if (scene_depth <= 0.0) {
			continue; // The sky: nothing to pass behind.
		}
		vec3 scene = view_position(screen.xy, scene_depth);
		float behind = scene.z - at.z;
		if (behind > 0.0 && behind < step_length * 2.5) {
			found = 1.0;
			return textureLod(colour_copy, screen.xy, 0.0).rgb;
		}
	}
	return textureLod(colour_copy, last_uv, 0.0).rgb;
}

void main() {
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
	if (pixel.x >= int(params.screen.x) || pixel.y >= int(params.screen.y)) {
		return;
	}
	float wetness = params.weather.x;
	if (wetness <= 0.001) {
		return;
	}
	vec2 uv = (vec2(pixel) + 0.5) / params.screen.xy;
	float depth = textureLod(depth_texture, uv, 0.0).r;
	if (depth <= 0.0) {
		return; // The sky.
	}
	vec3 view = view_position(uv, depth);
	vec4 packed = textureLod(normal_roughness, uv, 0.0);
	float roughness = packed.w > 0.5 ? 1.0 - packed.w : packed.w;
	roughness /= (127.0 / 255.0);
	vec3 view_normal = normalize(packed.xyz * 2.0 - 1.0);
	vec3 world = (params.view_to_world * vec4(view, 1.0)).xyz;
	vec3 normal = normalize(mat3(params.view_to_world) * view_normal);
	// A wall's rain comes from beside it: looked for a little out from its face.
	if (sheltered(world + normal * 0.6 + vec3(0.0, 0.05, 0.0))) {
		return;
	}
	vec4 original = textureLod(colour_copy, uv, 0.0);
	vec3 colour = original.rgb;
	float facing_up = normal.y;
	// Walls and slopes get less wet than flat ground; smooth things (metal, glass) don't darken.
	float wet = wetness * mix(0.45, 1.0, smoothstep(0.0, 0.7, facing_up));
	float porous = smoothstep(0.05, 0.5, roughness);
	float darker = 1.0 - params.screen.z * wet * porous;
	// Puddles on flat ground, more of it the wetter it gets: low spots by the world's own noise.
	float flat_ground = smoothstep(0.9, 0.97, facing_up);
	float lows = fbm(world.xz * 0.18) * 0.7 + fbm(world.xz * 0.9) * 0.3;
	float covered = params.weather.w * smoothstep(0.25, 1.0, wetness);
	float puddle = flat_ground * smoothstep(1.0 - covered, 1.0 - covered + 0.06, lows);
	// The water's surface: still in a puddle, rippled while it rains, and only a film elsewhere.
	vec3 surface = normal;
	if (params.weather.y > 0.01 && puddle > 0.0) {
		vec2 tilt = ripples(world.xz, params.weather.z) * params.weather.y;
		vec3 rippled = normalize(vec3(tilt.x, 1.0, tilt.y));
		surface = normalize(mix(normal, rippled, puddle));
	} else {
		surface = normalize(mix(normal, vec3(0.0, 1.0, 0.0), puddle));
	}
	vec3 view_surface = normalize(transpose(mat3(params.view_to_world)) * surface);
	vec3 to_eye = normalize(-view);
	float cosine = max(dot(view_surface, to_eye), 0.0);
	float fresnel = 0.02 + 0.98 * pow(1.0 - cosine, 5.0);
	// A film of water reflects a little, more where it's smooth already; a puddle like a mirror.
	float sheen = mix(wet * 0.35 * (1.0 - roughness * 0.6), 1.0, puddle) * params.screen.w;
	float reflectance = fresnel * sheen;
	vec3 result = colour * mix(darker, 0.45, puddle);
	if (reflectance > 0.004) {
		float found;
		vec3 mirrored = reflected(view, reflect(-to_eye, view_surface), found);
		result = mix(result, mirrored, clamp(reflectance, 0.0, 1.0));
	}
	imageStore(colour_image, pixel, vec4(result, original.a));
}
