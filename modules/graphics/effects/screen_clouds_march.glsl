#[compute]

#version 450

#VERSION_DEFINES

// Volumetric clouds (AtmosphereSkyMaterial), raymarched as
// Schneider's Nubis and Hillaire's Frostbite clouds are, into a dome of
// directions round the camera rather than the screen: a sixteenth of its
// texels each frame, in a 4 by 4 order, so a texel is at most sixteen frames
// old. Clouds are a kilometre and more off, so one dome serves both eyes of a
// headset, the sky's reflections and every mirror alike, with nothing worked
// out per view. Each texel holds the light the clouds and the air before
// them add (rgb, the air's inscattered light over what they hide included)
// and how much of the sky behind still shows (a); AtmosphereSkyMaterial lays it
// over the sky.

#include "atmosphere_inc.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
layout(rgba16f, set = 0, binding = 1) uniform restrict image2D dome;
layout(set = 0, binding = 2) uniform sampler2D transmittance_table;
layout(set = 0, binding = 3) uniform sampler2D multi_scattering_table;
layout(set = 0, binding = 4) uniform sampler2D sky_view_table;
layout(set = 0, binding = 5) uniform sampler3D shape_noise;
layout(set = 0, binding = 6) uniform sampler3D detail_noise;

#include "atmosphere_march_inc.glsl"

layout(push_constant, std430) uniform Params {
	vec4 camera;  // x, z the camera on the map (km east, km south), y its height over the ground (km), w this frame's turn (0 to 15)
	vec4 weather;  // x the clouds' cover (0 to 1), y how much is one low grey deck (overcast), z rain, w density
	vec4 wind;  // xy how far the clouds have drifted (km), z the shape's scale (km across a tile), w the detail's
	vec4 layer;  // x the cloud layer's bottom, y its top over the ground (km), z the furthest a ray goes (km), w how dark it is (0 day, 1 night)
	vec4 glow;  // rgb a night sky's glow on the clouds' undersides, a the even sky light on them by day
} params;

const int STEPS = 64;
const int LIGHT_STEPS = 6;
const vec2 SKY_VIEW_SIZE = vec2(192.0, 108.0);

// The dome's texel for a direction above the horizon: x round from the east, y from the zenith (0) to the
// horizon (1), squared so its texels crowd towards the horizon, where the clouds lie thinnest on screen.
vec3 dome_direction(vec2 uv) {
	float around = uv.x * 2.0 * PI;
	float up = (1.0 - uv.y) * (1.0 - uv.y);
	float flat_length = sqrt(max(1.0 - up * up, 0.0));
	return vec3(cos(around) * flat_length, up, sin(around) * flat_length);
}

uint pcg(uint v) {
	uint state = v * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

float remap(float v, float from_low, float from_high, float to_low, float to_high) {
	return to_low + (v - from_low) / max(from_high - from_low, 1e-5) * (to_high - to_low);
}

// How cloudy the sky is over a point on the map (km): fields of cloud and clear gaps between them, drifting with
// the wind, gathered by a wider pattern still, so clouds come in groups of their own sizes, never one even
// scatter of like puffs.
float cover_at(vec2 at) {
	vec2 p = (at + params.wind.xy) / (params.wind.z * 4.0);
	float fields = textureLod(shape_noise, vec3(p * 0.3, 0.13), 0.0).r;
	float broad = textureLod(shape_noise, vec3(p, 0.37), 0.0).r * 0.65 + textureLod(shape_noise, vec3(p * 2.7, 0.71), 0.0).g * 0.35;
	float cover = params.weather.x * mix(0.3, 1.15, smoothstep(0.3, 0.75, fields));
	return clamp(remap(broad, 1.0 - cover, 1.0, 0.0, 1.0) + params.weather.y, 0.0, 1.0);
}

// The density at a point (km: x east, y over the ground, z south), and its share up the layer.
float density_at(vec3 p, float cheap) {
	float height = clamp((p.y - params.layer.x) / max(params.layer.y - params.layer.x, 1e-3), 0.0, 1.0);
	// Cumulus round off at the top and flatten at the base; an overcast deck is a low flat slab.
	float cumulus = smoothstep(0.0, 0.1, height) * smoothstep(1.0, 0.45, height);
	float deck = smoothstep(0.0, 0.05, height) * smoothstep(0.45, 0.25, height);
	float profile = mix(cumulus, deck, params.weather.y);
	float cover = cover_at(p.xz) * profile;
	if (cover <= 0.0) {
		return 0.0;
	}
	vec3 at = (p + vec3(params.wind.x, 0.0, params.wind.y)) / params.wind.z;
	vec4 shape = textureLod(shape_noise, at, 0.0);
	float worleys = shape.g * 0.625 + shape.b * 0.25 + shape.a * 0.125;
	float base = remap(shape.r, worleys - 1.0, 1.0, 0.0, 1.0);
	float density = clamp(remap(base, 1.0 - cover, 1.0, 0.0, 1.0), 0.0, 1.0);
	if (density <= 0.0 || cheap > 0.5) {
		return density * params.weather.w;
	}
	vec3 detail_sample = textureLod(detail_noise, (p + vec3(params.wind.x, 0.0, params.wind.y) * 1.3) / params.wind.w, 0.0).rgb;
	float detail = detail_sample.r * 0.625 + detail_sample.g * 0.25 + detail_sample.b * 0.125;
	// Wisps at the base, billows higher up.
	detail = mix(detail, 1.0 - detail, clamp(height * 4.0, 0.0, 1.0));
	// Eroded hard at the edges, so a cloud frays out rather than rounding off like cotton wool.
	density = clamp(remap(density, detail * 0.5, 1.0, 0.0, 1.0), 0.0, 1.0);
	return density * params.weather.w;
}

// Henyey-Greenstein, two lobes: the silver lining towards the sun and some light thrown back.
float hg(float cos_theta, float g) {
	float g2 = g * g;
	return (1.0 - g2) / (4.0 * PI * pow(max(1.0 + g2 - 2.0 * g * cos_theta, 1e-4), 1.5));
}

vec3 sky_view_at(vec3 d) {
	float view_r = bottom_radius() + params.camera.y;
	vec2 flat_eye = d.xz;
	vec2 flat_sun = atmosphere.sun.xz;
	float eye_length = length(flat_eye);
	float sun_length = length(flat_sun);
	float light_view_cos = (eye_length > 1e-4 && sun_length > 1e-4) ? dot(flat_eye / eye_length, flat_sun / sun_length) : 1.0;
	vec2 uv = sky_view_uv(false, max(d.y, 0.0005), light_view_cos, view_r);
	return textureLod(sky_view_table, to_texel_middles(uv, SKY_VIEW_SIZE), 0.0).rgb;
}

const float RAIN_DECK = 0.4;  // of that under rain: a rain cloud is thicker and darker
const float DECK_LIGHT = 9.0;  // an overcast deck's underside, times the clear zenith's brightness over it

void main() {
	ivec2 size = imageSize(dome);
	// This frame's turn: every 4th texel across and down, offset by it.
	int turn = int(params.camera.w);
	ivec2 pixel = ivec2(gl_GlobalInvocationID.xy) * 4 + ivec2(turn % 4, turn / 4);
	if (pixel.x >= size.x || pixel.y >= size.y) {
		return;
	}
	vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
	vec3 d = dome_direction(uv);
	vec3 sky = sky_view_at(d);
	// The camera in the planet's frame (its middle at the origin), and on the map.
	vec3 o = vec3(0.0, bottom_radius() + params.camera.y, 0.0);
	float t_in = ray_sphere(o, d, bottom_radius() + params.layer.x);
	float t_out = ray_sphere(o, d, bottom_radius() + params.layer.y);
	if (t_in < 0.0 || t_out < 0.0 || t_in > params.layer.z) {
		imageStore(dome, pixel, vec4(0.0, 0.0, 0.0, 1.0));
		return;
	}
	t_out = min(t_out, t_in + params.layer.z * 0.5);
	t_out = min(t_out, params.layer.z);
	vec3 to_sun = atmosphere.sun.xyz;
	float cos_theta = dot(d, to_sun);
	float phase = mix(hg(cos_theta, 0.8), hg(cos_theta, -0.25), 0.3);
	// The light falling on the clouds: the sun through the air down to them, the sky's from above, a night sky's glow from below.
	vec3 zenith_sky = sky_view_at(vec3(0.0, 1.0, 0.0));
	// Through one grey deck the light comes down white, whatever the sky over it: every colour scattered over and
	// over on the way through. Lit by the zenith's blue, an overcast was a dark blue lid.
	zenith_sky = mix(zenith_sky, vec3(dot(zenith_sky, vec3(0.2126, 0.7152, 0.0722))) * DECK_LIGHT * mix(1.0, RAIN_DECK, params.weather.z), params.weather.y);
	vec3 light = vec3(0.0);
	float transmittance = 1.0;
	float distance_sum = 0.0;
	float weight_sum = 0.0;
	float dt = (t_out - t_in) / float(STEPS);
	// One fixed offset a texel (its own hash): a jitter that never changes, so nothing flickers, and no bands.
	float offset = float(pcg(uint(pixel.x) ^ pcg(uint(pixel.y))) & 1023u) / 1023.0;
	for (int i = 0; i < STEPS; i++) {
		float t = t_in + (float(i) + offset) * dt;
		vec3 planet = o + d * t;
		float height = length(planet) - bottom_radius();
		vec3 p = vec3(params.camera.x + d.x * t, height, params.camera.z + d.z * t);
		float density = density_at(p, 0.0);
		if (density <= 0.0) {
			continue;
		}
		// Towards the sun through the cloud, a few steps growing longer.
		float optical = 0.0;
		float light_step = 0.08;
		vec3 q = p;
		for (int k = 0; k < LIGHT_STEPS; k++) {
			q += vec3(to_sun.x, to_sun.y, to_sun.z) * light_step;
			optical += density_at(q, 1.0) * light_step;
			light_step *= 1.6;
		}
		float extinction = density * 60.0;  // a km of the thickest cloud
		float step_transmittance = exp(-extinction * dt);
		// Several orders of scattering at once (Hillaire's approximation): each weaker, less absorbed, less forward.
		vec3 sun_light = sun_transmittance(planet, to_sun) * planet_shadow(planet, to_sun) * atmosphere.sun.w;
		float scattered = 0.0;
		float a = 1.0;
		float b = 1.0;
		float c = 1.0;
		for (int order = 0; order < 3; order++) {
			scattered += a * exp(-optical * 60.0 * b) * mix(1.0 / (4.0 * PI), phase, c);
			a *= 0.5;
			b *= 0.4;
			c *= 0.5;
		}
		float up = clamp((p.y - params.layer.x) / max(params.layer.y - params.layer.x, 1e-3), 0.0, 1.0);
		vec3 ambient = zenith_sky * params.glow.a * mix(0.35, 1.0, up) + params.glow.rgb * (1.0 - up) * params.layer.w;
		vec3 in_light = (sun_light * scattered + ambient) * extinction;
		light += transmittance * (in_light - in_light * step_transmittance) / max(extinction, 1e-5);
		distance_sum += t * transmittance * (1.0 - step_transmittance);
		weight_sum += transmittance * (1.0 - step_transmittance);
		transmittance *= step_transmittance;
		if (transmittance < 0.01) {
			transmittance = 0.0;
			break;
		}
	}
	// The air between the camera and the clouds: through it the clouds fade into the sky behind them.
	float cloud_at = weight_sum > 1e-4 ? distance_sum / weight_sum : t_in;
	// Far off they thin into the haze, so the furthest never stack into a band along the horizon.
	float far = smoothstep(params.layer.z * 0.15, params.layer.z * 0.55, cloud_at);
	transmittance = mix(transmittance, 1.0, far);
	light *= 1.0 - far;
	vec3 far_point = o + d * cloud_at;
	vec3 through_camera = sun_transmittance(o, d);
	vec3 through_cloud = sun_transmittance(far_point, d);
	vec3 air = clamp(through_camera / max(through_cloud, vec3(1e-4)), 0.0, 1.0);
	float air_share = dot(air, vec3(1.0 / 3.0));
	vec3 added = light * air + sky * (1.0 - transmittance) * (1.0 - air_share);
	imageStore(dome, pixel, vec4(added, transmittance));
}
