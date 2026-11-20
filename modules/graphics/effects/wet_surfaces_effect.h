#pragma once

// WetSurfacesEffect: rain on the world's surfaces, for every material. What the rain can reach
// darkens as Weather3D's wetness rises, takes on a sheen, and flat ground gathers puddles that
// reflect what's on screen and ripple while it rains. Under roofs and bridges, in the weather's
// height map of what's overhead, stays dry. Drawn after the sky and before transparent things.

#include "screen_effect.h"

#include "screen_wet_surfaces.glsl.gen.h"

class WetSurfacesEffect : public CompositorEffect {
	GDCLASS(WetSurfacesEffect, CompositorEffect);

	struct State {
		ScreenShader<ScreenWetSurfacesShaderRD> shader;
		ScreenEffect::ColourCopy copy;
		RID params[2]; // One for each view.
		RID no_shelter; // A height map with nothing overhead, while there's no weather.
		~State();
	};

	float darkening = 0.45;
	float puddles = 0.35;
	float reflections = 1.0;
	State *state = nullptr; // The render thread's.

	void _draw(int p_callback_type, const RenderData *p_render_data);

protected:
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();

public:
	void set_darkening(float p_darkening);
	float get_darkening() const;
	void set_puddles(float p_puddles);
	float get_puddles() const;
	void set_reflections(float p_reflections);
	float get_reflections() const;

	WetSurfacesEffect();
	~WetSurfacesEffect();
};
