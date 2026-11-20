#pragma once

// LightShaftsEffect: shafts of sunlight through gaps between buildings, trees and clouds, towards the
// sun on screen: from each pixel, a march towards the sun counting the open sky it passes.

#include "screen_effect.h"

#include "screen_light_shafts.glsl.gen.h"

class LightShaftsEffect : public CompositorEffect {
	GDCLASS(LightShaftsEffect, CompositorEffect);

	struct State {
		ScreenShader<ScreenLightShaftsShaderRD> shader;
	};

	float strength = 0.3;
	Color color = Color(1, 1, 1);
	float decay = 0.96;
	int samples = 32;
	float reach = 0.8;
	float spread = 0.9;
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
	void set_decay(float p_decay);
	float get_decay() const;
	void set_samples(int p_samples);
	int get_samples() const;
	void set_reach(float p_reach);
	float get_reach() const;
	void set_spread(float p_spread);
	float get_spread() const;

	LightShaftsEffect();
	~LightShaftsEffect();
};
