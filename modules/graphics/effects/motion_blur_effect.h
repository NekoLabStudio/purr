#pragma once

// MotionBlurEffect: what moves across the screen smeared along its motion, over the shutter's
// share of the frame, from the renderer's motion vectors.

#include "screen_effect.h"

#include "screen_motion_blur.glsl.gen.h"

class MotionBlurEffect : public CompositorEffect {
	GDCLASS(MotionBlurEffect, CompositorEffect);

	struct State {
		ScreenShader<ScreenMotionBlurShaderRD> shader;
		ScreenEffect::ColourCopy copy;
	};

	float shutter = 0.5;
	int samples = 8;
	float max_length = 0.04;
	State *state = nullptr; // The render thread's.

	void _draw(int p_callback_type, const RenderData *p_render_data);

protected:
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();

public:
	void set_shutter(float p_shutter);
	float get_shutter() const;
	void set_samples(int p_samples);
	int get_samples() const;
	void set_max_length(float p_length);
	float get_max_length() const;

	MotionBlurEffect();
	~MotionBlurEffect();
};
