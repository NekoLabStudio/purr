#pragma once

// LocalExposureEffect: before tonemapping, the light round each pixel (a bilateral grid of the
// picture's brightness, blurred) is brought towards middle grey, highlights and shadows each by
// a contrast of their own, so a lit sign keeps its lettering and a dark alley still reads at
// night. The scene's exposure still sets the picture's brightness overall.

#include "screen_effect.h"

#include "screen_local_exposure_apply.glsl.gen.h"
#include "screen_local_exposure_blur.glsl.gen.h"
#include "screen_local_exposure_grid.glsl.gen.h"

class LocalExposureEffect : public CompositorEffect {
	GDCLASS(LocalExposureEffect, CompositorEffect);

	static constexpr int CELL = 32;
	static constexpr int BINS = 32;
	static constexpr float DARKEST = -12.0; // The log2 brightness the bands cover, before exposure.
	static constexpr float BRIGHTEST = 6.0;

	struct State {
		ScreenShader<ScreenLocalExposureGridShaderRD> grid;
		ScreenShader<ScreenLocalExposureBlurShaderRD> blur;
		ScreenShader<ScreenLocalExposureApplyShaderRD> apply;
		RID grid_texture;
		RID spare_texture;
		Vector2i cells;
		~State();
	};

	float highlights = 0.8;
	float shadows = 0.85;
	float detail = 0.6;
	State *state = nullptr; // The render thread's.

	void _draw(int p_callback_type, const RenderData *p_render_data);
	RID _grid(RID &r_texture, const Vector2i &p_cells);

protected:
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();

public:
	void set_highlights(float p_contrast);
	float get_highlights() const;
	void set_shadows(float p_contrast);
	float get_shadows() const;
	void set_detail(float p_detail);
	float get_detail() const;

	LocalExposureEffect();
	~LocalExposureEffect();
};
