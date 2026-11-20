#pragma once

// The night sky of Purr's skies (CloudySkyMaterial, AtmosphereSkyMaterial), as sky shader code:
// stars and the Milky Way turning with the sky through the night, and the moon in its phase, lit by
// the sun from where the sun is. TimeOfDay sets the globals it reads; without one, the stars stand
// still and there's no moon. A sky shader including this declares the globals and the cloud code
// (sky_clouds_shader.h) first.

inline constexpr const char *SKY_NIGHT_SHADER_GLOBALS = R"(
global uniform vec3 time_of_day_moon_direction;
global uniform mat3 time_of_day_star_rotation;
)";

inline constexpr const char *SKY_NIGHT_SHADER_CODE = R"(
// One star or none in each cell of a grid round the sky: where in the cell, how bright, what colour.
vec3 star_layer(vec3 d, float cells, float chance, float size) {
	vec3 p = d * cells;
	vec3 cell = floor(p);
	uint seed = cloud_pcg(uint(int(cell.x) + 4096) ^ cloud_pcg(uint(int(cell.y) + 4096) ^ cloud_pcg(uint(int(cell.z) + 4096))));
	float roll = float(seed & 0xffffu) / 65535.0;
	if (roll > chance) {
		return vec3(0.0);
	}
	// Kept off the cell's edges, so no star is cut in half there.
	vec3 offset = vec3(float((seed >> 16u) & 0xffu), float((seed >> 24u) & 0xffu), float(cloud_pcg(seed) & 0xffu)) / 255.0;
	vec3 star = normalize((cell + 0.25 + offset * 0.5) / cells);
	float apart = acos(clamp(dot(d, star), -1.0, 1.0));
	float shine = pow(roll / chance, 6.0);
	float glow = exp(-apart * apart / (size * size));
	float warmth = float(cloud_pcg(seed + 7u) & 0xffu) / 255.0;
	vec3 colour = mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.85, 0.65), warmth);
	return colour * glow * (0.15 + shine * 3.0);
}

// The stars, the Milky Way and the moon, over colour, looking along eye: night how dark it is,
// clear how much of the sky shows between clouds, night_level how bright this sky is at night
// and moon_level how bright the full moon's face is.
vec3 night_sky(vec3 colour, vec3 eye, vec3 sun, float night, float clear, float night_level, float moon_level) {
	if (eye.y <= -0.02) {
		return colour;
	}
	// Dimmer through the thick air at the horizon.
	float through = smoothstep(-0.02, 0.25, eye.y) * clear;
	vec3 d = transpose(time_of_day_star_rotation) * eye;
	if (length(time_of_day_star_rotation[0]) < 0.5) {
		d = eye; // No TimeOfDay: still stars.
	}
	if (night > 0.0) {
		// A band of faint light and darker dust lanes round a great circle tilted to the stars' pole.
		vec3 band_axis = normalize(vec3(0.46, 0.33, 0.82));
		float off_band = dot(d, band_axis);
		float band = exp(-off_band * off_band * 22.0);
		float dust = cloud_fbm(d.xy * 9.0 + d.z * 3.0, 4) * cloud_fbm(d.yz * 7.0 - d.x * 2.0, 3);
		vec3 milky = vec3(0.55, 0.6, 0.75) * band * (0.2 + 0.8 * smoothstep(0.12, 0.4, dust));
		vec3 stars = star_layer(d, 150.0, 0.25, 0.0011) + star_layer(d, 420.0, 0.12 + band * 0.4, 0.0006) * 0.5;
		colour += (milky + stars * 4.0) * night_level * night * through;
	}
	// The moon: a sphere lit from the sun's side, so its phase is where the sun is.
	vec3 moon = time_of_day_moon_direction;
	if (length(moon) > 0.5) {
		moon = normalize(moon);
		float radius = 0.0085;
		vec3 off = eye - moon * dot(eye, moon);
		float x = length(off) / radius;
		if (x < 1.0 && dot(eye, moon) > 0.0) {
			vec3 across = off / max(length(off), 1e-6);
			vec3 normal = normalize(across * x - moon * sqrt(max(1.0 - x * x, 0.0)));
			float lit = smoothstep(-0.04, 0.12, dot(normal, sun));
			// Darker seas over brighter highlands, fixed to the moon's face.
			float seas = cloud_fbm(normal.xy * 3.0 + normal.z * 1.7 + 11.0, 4);
			vec3 albedo = vec3(0.98, 0.96, 0.92) * mix(0.55, 1.0, smoothstep(0.35, 0.6, seas));
			float edge = 1.0 - smoothstep(0.92, 1.0, x);
			float earthshine = 0.015;
			vec3 face = albedo * (lit + earthshine) * edge;
			// Paler against the day's sky, as the real moon is.
			colour += face * moon_level * mix(0.25, 1.0, night) * through;
		}
		// A faint halo round it, as bright as its phase.
		float phase = 0.5 - 0.5 * dot(moon, sun);
		float facing = max(dot(eye, moon), 0.0);
		colour += vec3(0.6, 0.65, 0.75) * pow(facing, 400.0) * 0.04 * phase * moon_level * night * through;
	}
	return colour;
}
)";
