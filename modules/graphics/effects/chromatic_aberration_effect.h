#pragma once

// ChromaticAberrationEffect: red and blue spread apart towards the edges of the picture, as a lens
// bends each colour a little differently.

#include "screen_effect.h"

#include "screen_chromatic_aberration.glsl.gen.h"

class ChromaticAberrationEffect : public CompositorEffect {
	GDCLASS(ChromaticAberrationEffect, CompositorEffect);

	struct State {
		ScreenShader<ScreenChromaticAberrationShaderRD> shader;
		ScreenEffect::ColourCopy copy;
	};

	float strength = 0.01;
	State *state = nullptr; // The render thread's.

	void _draw(int p_callback_type, const RenderData *p_render_data);

protected:
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();

public:
	void set_strength(float p_strength);
	float get_strength() const;

	ChromaticAberrationEffect();
	~ChromaticAberrationEffect();
};
