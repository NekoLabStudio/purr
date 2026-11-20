#pragma once

// VignetteEffect: the picture darkening towards its corners, as through a real lens.

#include "screen_effect.h"

#include "screen_vignette.glsl.gen.h"

class VignetteEffect : public CompositorEffect {
	GDCLASS(VignetteEffect, CompositorEffect);

	struct State {
		ScreenShader<ScreenVignetteShaderRD> shader;
	};

	float strength = 0.3;
	float softness = 0.6;
	Color color = Color(0, 0, 0);
	State *state = nullptr; // The render thread's.

	void _draw(int p_callback_type, const RenderData *p_render_data);

protected:
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();

public:
	void set_strength(float p_strength);
	float get_strength() const;
	void set_softness(float p_softness);
	float get_softness() const;
	void set_color(Color p_color);
	Color get_color() const;

	VignetteEffect();
	~VignetteEffect();
};
