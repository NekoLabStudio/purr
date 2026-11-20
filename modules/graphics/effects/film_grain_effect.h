#pragma once

// FilmGrainEffect: fine noise that changes every frame, strongest in the mid and dark tones, as on film.

#include "screen_effect.h"

#include "screen_film_grain.glsl.gen.h"

class FilmGrainEffect : public CompositorEffect {
	GDCLASS(FilmGrainEffect, CompositorEffect);

	struct State {
		ScreenShader<ScreenFilmGrainShaderRD> shader;
	};

	float amount = 0.05;
	float grain_size = 1.0;
	bool colored = false;
	State *state = nullptr; // The render thread's.

	void _draw(int p_callback_type, const RenderData *p_render_data);

protected:
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();

public:
	void set_amount(float p_amount);
	float get_amount() const;
	void set_grain_size(float p_grain_size);
	float get_grain_size() const;
	void set_colored(bool p_colored);
	bool is_colored() const;

	FilmGrainEffect();
	~FilmGrainEffect();
};
