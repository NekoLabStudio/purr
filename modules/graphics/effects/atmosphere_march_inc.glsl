// Marching through the atmosphere (atmosphere_inc.glsl): what light a ray
// gathers and how much of what lies past it gets through. Needs the
// transmittance table bound as transmittance_table, and unless this is the
// multiple scattering pass (MULTI_SCATTERING_PASS), the multiple scattering
// table as multi_scattering_table.

const vec2 TRANSMITTANCE_SIZE = vec2(256.0, 64.0);
const vec2 MULTI_SCATTERING_SIZE = vec2(32.0, 32.0);

// How much of the sun's light reaches a point (planet's middle at the origin), the planet's shadow apart.
vec3 sun_transmittance(vec3 p, vec3 to_sun) {
	float r = max(length(p), 1e-3);
	float mu = dot(p / r, to_sun);
	return textureLod(transmittance_table, to_texel_middles(transmittance_uv(r, mu), TRANSMITTANCE_SIZE), 0.0).rgb;
}

float planet_shadow(vec3 p, vec3 to_sun) {
	return ray_sphere(p, to_sun, bottom_radius()) > 0.0 ? 0.0 : 1.0;
}

#ifndef MULTI_SCATTERING_PASS
// Light scattered more than once, as an even glow by height and the sun's height (Hillaire 5.5).
vec3 multi_scattered(vec3 p, vec3 to_sun) {
	float r = max(length(p), 1e-3);
	float mu = dot(p / r, to_sun);
	vec2 uv = vec2(mu * 0.5 + 0.5, (r - bottom_radius()) / max(top_radius() - bottom_radius(), 1e-3));
	return textureLod(multi_scattering_table, to_texel_middles(clamp(uv, 0.0, 1.0), MULTI_SCATTERING_SIZE), 0.0).rgb;
}

// The light a point's air sends along a ray, a km of air: the sun's, once
// and many times scattered, and the even light on the haze (the ambient:
// a night sky's glow, a cloud deck's grey), which lie low: on the
// whole height of the air, the night sky glowed blue.
vec3 in_scattered(vec3 p, vec3 to_sun, Medium m, float phase_r, float phase_m) {
	vec3 sun = sun_transmittance(p, to_sun) * planet_shadow(p, to_sun) * (m.rayleigh * phase_r + vec3(m.mie * phase_m));
	return (sun + multi_scattered(p, to_sun) * m.scattering) * atmosphere.sun.w + m.mie * atmosphere.ambient.rgb;
}
#endif

struct March {
	vec3 light;
	vec3 transmittance;
	vec3 multi;  // the multiple scattering pass: the share scattered on (Hillaire's f_ms)
};

// A ray from o (km from the planet's middle) along unit d, to the top of the
// atmosphere or the ground, or limit km if that's sooner (0: no limit).
March march(vec3 o, vec3 d, vec3 to_sun, float limit, int steps) {
	March result;
	result.light = vec3(0.0);
	result.transmittance = vec3(1.0);
	result.multi = vec3(0.0);
	float to_top = ray_sphere(o, d, top_radius());
	if (to_top < 0.0) {
		return result;
	}
	float to_ground = ray_sphere(o, d, bottom_radius());
	float t_max = to_top;
	bool ground = false;
	if (to_ground > 0.0 && to_ground < t_max) {
		t_max = to_ground;
		ground = true;
	}
	if (limit > 0.0 && limit < t_max) {
		t_max = limit;
		ground = false;
	}
	float cos_theta = dot(d, to_sun);
	float phase_r = rayleigh_phase(cos_theta);
	float phase_m = mie_phase(cos_theta, atmosphere.mie.w);
	for (int i = 0; i < steps; i++) {
		float t;
		float dt;
		ray_step(i, steps, t_max, t, dt);
		vec3 p = o + d * t;
		Medium m = medium_at(length(p) - bottom_radius());
		vec3 extinction = max(m.extinction, vec3(1e-7));
		vec3 step_transmittance = exp(-extinction * dt);
#ifdef MULTI_SCATTERING_PASS
		vec3 scattered = sun_transmittance(p, to_sun) * planet_shadow(p, to_sun) * m.scattering / (4.0 * PI);
		result.multi += result.transmittance * (m.scattering - m.scattering * step_transmittance) / extinction;
#else
		vec3 scattered = in_scattered(p, to_sun, m, phase_r, phase_m);
#endif
		// Integrated over the step as Hillaire does, so a thick step neither overshoots nor loses light.
		result.light += result.transmittance * (scattered - scattered * step_transmittance) / extinction;
		result.transmittance *= step_transmittance;
	}
#ifdef MULTI_SCATTERING_PASS
	if (ground) {
		vec3 p = o + d * t_max;
		vec3 up = p / bottom_radius();  // on the ground
		result.light += result.transmittance * sun_transmittance(p, to_sun) * max(dot(up, to_sun), 0.0) * atmosphere.ground.rgb / PI;
	}
#endif
	return result;
}
