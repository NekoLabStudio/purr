// The atmosphere, after Hillaire's "A Scalable and Production
// Ready Sky and Atmosphere Rendering Technique" (2020): the air's Rayleigh
// scattering, the haze's (Mie) and ozone's absorption over a planet, in
// kilometres, y up, the camera above its middle. Shared by the tables
// (screen_atmosphere_*.glsl) and the aerial perspective (screen_aerial_*.glsl); its figures
// come from AtmosphereSkyMaterial.
// Every division and square root here is guarded: a NaN draws black, and the
// upscaler smears it across the screen.

#define PI 3.14159265358979

layout(set = 0, binding = 0, std140) uniform AtmosphereData {
	vec4 rayleigh;  // rgb scattering a km at the ground, a: scale height (km)
	vec4 mie;  // x scattering, y extinction a km at the ground, z scale height (km), w the phase's anisotropy
	vec4 ozone;  // rgb absorption a km at its peak, a: its peak's height (km)
	vec4 radii;  // x the ground's, y the top's (km), z the camera's height over the ground (km), w the ozone layer's half width (km)
	vec4 sun;  // xyz towards the sun, w its illuminance in the scene's units
	vec4 ambient;  // rgb an even light on the air (a night sky's glow, an overcast sky's), in the scene's units
	vec4 ground;  // rgb the ground's albedo
} atmosphere;

float bottom_radius() {
	return atmosphere.radii.x;
}

float top_radius() {
	return atmosphere.radii.y;
}

struct Medium {
	vec3 rayleigh;  // scattering
	float mie;  // scattering
	vec3 scattering;
	vec3 extinction;
};

Medium medium_at(float height) {
	Medium m;
	float h = max(height, 0.0);
	m.rayleigh = atmosphere.rayleigh.rgb * exp(-h / max(atmosphere.rayleigh.a, 1e-3));
	float mie_density = exp(-h / max(atmosphere.mie.z, 1e-3));
	m.mie = atmosphere.mie.x * mie_density;
	vec3 ozone = atmosphere.ozone.rgb * max(0.0, 1.0 - abs(h - atmosphere.ozone.a) / max(atmosphere.radii.w, 1e-3));
	m.scattering = m.rayleigh + vec3(m.mie);
	m.extinction = m.rayleigh + vec3(atmosphere.mie.y * mie_density) + ozone;
	return m;
}

// How far along a ray (unit direction) from o it meets a sphere round the
// planet's middle of radius r, or -1 if it doesn't ahead.
float ray_sphere(vec3 o, vec3 d, float r) {
	float b = dot(o, d);
	float c = dot(o, o) - r * r;
	float disc = b * b - c;
	if (disc < 0.0) {
		return -1.0;
	}
	float s = sqrt(disc);
	float near = -b - s;
	float far = -b + s;
	if (near >= 0.0) {
		return near;
	}
	return far >= 0.0 ? far : -1.0;
}

float rayleigh_phase(float cos_theta) {
	return 3.0 / (16.0 * PI) * (1.0 + cos_theta * cos_theta);
}

// Cornette-Shanks, as Hillaire's.
float mie_phase(float cos_theta, float g) {
	float k = 3.0 / (8.0 * PI) * (1.0 - g * g) / (2.0 + g * g);
	float denominator = max(1.0 + g * g - 2.0 * g * cos_theta, 1e-4);
	return k * (1.0 + cos_theta * cos_theta) / (denominator * sqrt(denominator));
}

// Where step i of n along a ray t_max long lies, and how long it is: closer
// together near its start, where the haze lies low (it lies in the lowest few hundred metres): evenly spaced, a low sun's light through it came out
// several times too bright.
void ray_step(int i, int n, float t_max, out float t, out float dt) {
	float a = float(i) / float(n);
	float b = float(i + 1) / float(n);
	t = t_max * (a * a + b * b) * 0.5;
	dt = t_max * (b * b - a * a);
}

// A table's uv from where its texel's middle lies (0 to 1 across its texels), and back.
vec2 to_texel_middles(vec2 uv, vec2 size) {
	return (uv * (size - 1.0) + 0.5) / size;
}

vec2 from_texel_middles(vec2 uv, vec2 size) {
	return (uv * size - 0.5) / max(size - 1.0, vec2(1.0));
}

// The transmittance table: by height (r from the middle) and the cosine of the zenith angle (Bruneton's mapping).
vec2 transmittance_uv(float r, float mu) {
	float top = top_radius();
	float bottom = bottom_radius();
	float h = sqrt(max(top * top - bottom * bottom, 0.0));
	float rho = sqrt(max(r * r - bottom * bottom, 0.0));
	float discriminant = r * r * (mu * mu - 1.0) + top * top;
	float d = max(0.0, -r * mu + sqrt(max(discriminant, 0.0)));
	float d_min = top - r;
	float d_max = rho + h;
	return vec2((d - d_min) / max(d_max - d_min, 1e-6), rho / max(h, 1e-6));
}

void transmittance_r_mu(vec2 uv, out float r, out float mu) {
	float top = top_radius();
	float bottom = bottom_radius();
	float h = sqrt(max(top * top - bottom * bottom, 0.0));
	float rho = h * uv.y;
	r = sqrt(rho * rho + bottom * bottom);
	float d_min = top - r;
	float d_max = rho + h;
	float d = d_min + uv.x * (d_max - d_min);
	mu = d <= 1e-6 ? 1.0 : clamp((h * h - rho * rho - d * d) / (2.0 * r * d), -1.0, 1.0);
}

// The sky-view table (Hillaire 5.3): x the view's angle round from the sun's
// side (squared, so finer towards the sun), y its zenith angle, squared
// towards the horizon either side, the ground below its middle.
vec2 sky_view_uv(bool below_horizon, float view_zenith_cos, float light_view_cos, float view_r) {
	float horizon = sqrt(max(view_r * view_r - bottom_radius() * bottom_radius(), 0.0));
	float beta = acos(clamp(horizon / max(view_r, 1e-6), -1.0, 1.0));
	float zenith_horizon = PI - beta;
	float view_zenith = acos(clamp(view_zenith_cos, -1.0, 1.0));
	vec2 uv;
	if (!below_horizon) {
		float coord = 1.0 - sqrt(max(1.0 - view_zenith / max(zenith_horizon, 1e-6), 0.0));
		uv.y = coord * 0.5;
	} else {
		float coord = sqrt(clamp((view_zenith - zenith_horizon) / max(beta, 1e-6), 0.0, 1.0));
		uv.y = coord * 0.5 + 0.5;
	}
	uv.x = sqrt(clamp(-light_view_cos * 0.5 + 0.5, 0.0, 1.0));
	return uv;
}

void sky_view_angles(vec2 uv, float view_r, out float view_zenith_cos, out float light_view_cos) {
	float horizon = sqrt(max(view_r * view_r - bottom_radius() * bottom_radius(), 0.0));
	float beta = acos(clamp(horizon / max(view_r, 1e-6), -1.0, 1.0));
	float zenith_horizon = PI - beta;
	float view_zenith;
	if (uv.y < 0.5) {
		float coord = 1.0 - 2.0 * uv.y;
		coord = 1.0 - coord * coord;
		view_zenith = zenith_horizon * coord;
	} else {
		float coord = uv.y * 2.0 - 1.0;
		view_zenith = zenith_horizon + beta * coord * coord;
	}
	view_zenith_cos = cos(view_zenith);
	float x = uv.x * uv.x;
	light_view_cos = -(x * 2.0 - 1.0);
}
