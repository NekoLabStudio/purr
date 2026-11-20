#pragma once

// AtmosphereTables: the air's lookup tables (Hillaire, "A Scalable and Production Ready Sky and
// Atmosphere Rendering Technique", 2020), worked out on the GPU: how much light gets through the
// air (transmittance) and the light scattered more than once, again only when the air changes,
// and the sky as seen from the ground (sky view), as the sun moves. AtmosphereSkyMaterial sets the
// air and reads the sky view; AerialPerspectiveEffect reads the air and the first two tables. One
// set for the game: the sky the camera sees sets it.

#include "core/templates/vector.h"
#include "scene/resources/texture_rd.h"

#include "screen_effect.h"

#include "screen_atmosphere_multi_scattering.glsl.gen.h"
#include "screen_atmosphere_sky_view.glsl.gen.h"
#include "screen_atmosphere_transmittance.glsl.gen.h"
#include "screen_clouds_march.glsl.gen.h"
#include "screen_clouds_noise.glsl.gen.h"

class AtmosphereTables {
public:
	// The air's figures, as atmosphere_inc.glsl's AtmosphereData lays them out: seven vec4s.
	static constexpr int FIGURES = 28;
	static constexpr int SUN_FIGURES = 16; // Where the sun's figures start: they change the sky view only.
	static constexpr int AMBIENT_FIGURES = 20; // And the light on the air, the same.
	// The clouds' figures, as screen_clouds_march.glsl's Params lays them out: five vec4s.
	static constexpr int CLOUD_FIGURES = 20;

private:
	struct State {
		ScreenShader<ScreenAtmosphereTransmittanceShaderRD> transmittance_shader;
		ScreenShader<ScreenAtmosphereMultiScatteringShaderRD> multi_scattering_shader;
		ScreenShader<ScreenAtmosphereSkyViewShaderRD> sky_view_shader;
		RID figures_buffer;
		RID transmittance;
		RID multi_scattering;
		RID sky_view;
		PackedFloat32Array lasting; // The figures the first two tables were made for.
		float camera_height = 0.04; // km, for AerialPerspectiveEffect.
		// The volumetric clouds: a dome of directions round the camera, a sixteenth of it worked out
		// each frame, from two tiling noise volumes made once.
		ScreenShader<ScreenCloudsNoiseShaderRD> noise_shader;
		ScreenShader<ScreenCloudsMarchShaderRD> march_shader;
		RID dome;
		RID shape_noise;
		RID detail_noise;
		RID repeating_sampler;
		int turn = 0;
		bool dome_filled = false;
		~State();
	};

	static AtmosphereTables *singleton;
	State *state = nullptr; // The render thread's.
	PackedFloat32Array figures; // The main thread's copy, as last set.
	Ref<Texture2DRD> sky_view_texture;
	Ref<Texture2DRD> transmittance_texture;
	Ref<Texture2DRD> dome_texture;

	void _update(const PackedFloat32Array &p_figures);
	static void _update_on_render_thread(uint64_t p_tables, const PackedFloat32Array &p_figures);
	static void _march_on_render_thread(uint64_t p_tables, const PackedFloat32Array &p_figures);
	void _march(const PackedFloat32Array &p_figures);
	RID _make_noise(int p_size, float p_kind, float p_cells);
	void _dispatch_table(RID p_shader, RID p_pipeline, RID p_table, const Size2i &p_size, const LocalVector<RID> &p_reads);

public:
	static AtmosphereTables *get_singleton();
	static void free_singleton();

	// Main thread: sets the air, making again on the render thread what it changes. Returns
	// whether anything changed, so the sky can draw its reflections again.
	bool set_figures(const PackedFloat32Array &p_figures);
	const PackedFloat32Array &get_figures() const { return figures; }
	Ref<Texture2DRD> get_sky_view_texture() const { return sky_view_texture; }
	Ref<Texture2DRD> get_transmittance_texture() const { return transmittance_texture; }
	Ref<Texture2DRD> get_dome_texture() const { return dome_texture; }
	// Main thread: works out the next sixteenth of the clouds' dome (all of it the first time).
	void step_clouds(const PackedFloat32Array &p_figures);

	// Render thread: for AerialPerspectiveEffect.
	bool is_ready() const { return state != nullptr && state->figures_buffer.is_valid(); }
	RID get_figures_buffer() const { return state ? state->figures_buffer : RID(); }
	RID get_transmittance() const { return state ? state->transmittance : RID(); }
	RID get_multi_scattering() const { return state ? state->multi_scattering : RID(); }
	float get_camera_height() const { return state ? state->camera_height : 0.04f; }

	AtmosphereTables();
	~AtmosphereTables();
};
