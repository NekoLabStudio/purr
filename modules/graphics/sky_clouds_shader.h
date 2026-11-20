#pragma once

// The painted clouds of Purr's skies (CloudySkyMaterial, AtmosphereSkyMaterial), as sky shader
// code: high thin cirrus drawn out along the wind, a layer of clouds lit from the sun's side, and
// the grey banks of an overcast sky. They drift by the global weather_cloud_drift (Weather3D's
// wind), which a sky shader including this declares, never by TIME: a sky that reads TIME is drawn
// again into the reflections every frame.

inline constexpr const char *SKY_CLOUDS_SHADER_CODE = R"(
// An integer hash: the same on every GPU, where sin()-based ones aren't.
uint cloud_pcg(uint v) {
	uint state = v * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

float cloud_hash(vec2 p) {
	uvec2 q = uvec2(ivec2(floor(p)));
	return float(cloud_pcg(q.x ^ cloud_pcg(q.y))) * (1.0 / 4294967296.0);
}

float cloud_noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(cloud_hash(i), cloud_hash(i + vec2(1.0, 0.0)), u.x), mix(cloud_hash(i + vec2(0.0, 1.0)), cloud_hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

float cloud_fbm(vec2 p, int octaves) {
	float value = 0.0;
	float amplitude = 0.5;
	for (int i = 0; i < octaves; i++) {
		value += amplitude * cloud_noise(p);
		p = p * 2.02 + vec2(7.0, 3.0);
		amplitude *= 0.5;
	}
	return value;
}

// Where a direction meets the clouds' plane overhead.
vec2 cloud_plane(vec3 eye) {
	return eye.xz / (max(eye.y, 0.0) + 0.12);
}

// The overcast's grey over a direction: lighter and darker banks, not a flat dome.
vec3 overcast_banks(vec3 eye, vec3 grey) {
	float up = max(eye.y, 0.0);
	return grey * (0.9 + 0.2 * cloud_fbm(cloud_plane(eye) * 0.35 + weather_cloud_drift * 0.5, 3)) * (0.85 + 0.15 * up);
}

// The cirrus and the cloud layer over colour, looking along eye (above the horizon): cover how much
// of the sky they take, overcast how much is a grey deck (low_cloud), night_cloud their colour at
// night, wisp_colour the cirrus's.
vec3 painted_clouds(vec3 colour, vec3 eye, vec3 sun, float night, float day, float cover, float overcast, float scale,
		vec3 wisp_colour, vec3 night_cloud, vec3 low_cloud) {
	if (eye.y <= 0.0) {
		return colour;
	}
	vec2 plane = cloud_plane(eye);
	vec2 flat_sun = sun.xz / max(length(sun.xz), 1e-4);
	float facing = max(dot(eye, sun), 0.0);
	float clear = 1.0 - overcast;
	vec2 high = mat2(vec2(0.8, 0.6), vec2(-0.6, 0.8)) * (plane * scale * 0.4 + weather_cloud_drift * 1.6);
	float wisps = smoothstep(0.55, 0.85, cloud_fbm(high * vec2(0.25, 1.0), 4)) * smoothstep(0.0, 0.3, eye.y);
	colour = mix(colour, wisp_colour, wisps * 0.35 * clear);
	// Their shapes warped, so they billow; each lit by how much cloud lies between it and the sun:
	// bright tops and edges on the sun's side, darker bellies, and at a low sun lit sides glowing
	// and bellies going violet.
	vec2 uv = plane * scale + weather_cloud_drift;
	vec2 warp = vec2(cloud_fbm(uv * 0.5 + vec2(3.1, 1.7), 3), cloud_fbm(uv * 0.5 + vec2(8.3, 2.8), 3)) - 0.5;
	vec2 at = uv + warp * 0.6;
	float covered = mix(cover, 0.95, overcast);
	float shape = cloud_fbm(at, 5);
	float density = smoothstep(1.0 - covered, 1.05 - covered * 0.5, shape) * smoothstep(0.0, 0.25, eye.y);
	float thick = smoothstep(1.0 - covered, 1.25 - covered * 0.5, shape);
	float sunward = cloud_fbm(at + flat_sun * 0.09, 4);
	float in_sun = clamp(0.55 - (sunward - shape) * 5.0, 0.0, 1.0) * (1.0 - night);
	float low_sun = 1.0 - smoothstep(0.05, 0.4, sun.y);
	vec3 sunlit = mix(vec3(1.0, 0.98, 0.94), vec3(1.0, 0.62, 0.38), low_sun) * (1.05 + 0.5 * pow(facing, 6.0) * (1.0 - thick));
	vec3 belly = mix(vec3(0.6, 0.64, 0.72), vec3(0.42, 0.34, 0.5), low_sun);
	vec3 day_cloud = mix(belly, sunlit, mix(in_sun, 1.0, 1.0 - thick) * mix(0.75, 1.0, 1.0 - thick));
	vec3 cloud = mix(night_cloud, day_cloud * mix(0.55, 1.0, day), 1.0 - night);
	cloud = mix(cloud, low_cloud * 0.9, overcast);
	return mix(colour, cloud, density * 0.92);
}
)";
