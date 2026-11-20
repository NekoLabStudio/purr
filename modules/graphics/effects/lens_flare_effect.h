#pragma once

// LensFlareEffect: the sun's light bouncing inside the camera's lens, as a glare round the sun and
// coloured ghosts across the screen, fading as the sun goes behind something.

#include "screen_effect.h"

#include "screen_lens_flare.glsl.gen.h"

class LensFlareEffect : public CompositorEffect {
	GDCLASS(LensFlareEffect, CompositorEffect);

	struct State {
		ScreenShader<ScreenLensFlareShaderRD> shader;
	};

	float strength = 0.2;
	Color color = Color(1, 1, 1);
	float glare_size = 0.15;
	float ghosts = 1.0;
	State *state = nullptr; // The render thread's.

	void _draw(int p_callback_type, const RenderData *p_render_data);

protected:
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();

public:
	void set_strength(float p_strength);
	float get_strength() const;
	void set_color(Color p_color);
	Color get_color() const;
	void set_glare_size(float p_glare_size);
	float get_glare_size() const;
	void set_ghosts(float p_ghosts);
	float get_ghosts() const;

	LensFlareEffect();
	~LensFlareEffect();
};
