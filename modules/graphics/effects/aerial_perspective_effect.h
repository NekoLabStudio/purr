#pragma once

// AerialPerspectiveEffect: the air between the camera and what it sees (Hillaire 2020), from
// AtmosphereSkyMaterial's air: far hills paler and bluer, a low sun's haze golden. For each view, a
// volume of the air's light and how much gets through, slice by slice out to `reach`, laid over
// the picture by each pixel's distance. Nothing is kept from frame to frame, so both eyes of a
// headset see the same air.

#include "screen_effect.h"

#include "screen_aerial_apply.glsl.gen.h"
#include "screen_aerial_volume.glsl.gen.h"

class AerialPerspectiveEffect : public CompositorEffect {
	GDCLASS(AerialPerspectiveEffect, CompositorEffect);

	static constexpr int CELLS = 32; // The volume's cells across, down and out.

	struct State {
		ScreenShader<ScreenAerialVolumeShaderRD> volume_shader;
		ScreenShader<ScreenAerialApplyShaderRD> apply_shader;
		RID volumes[2];
		~State();
	};

	float reach = 4000.0;
	State *state = nullptr; // The render thread's.

	void _draw(int p_callback_type, const RenderData *p_render_data);

protected:
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();

public:
	void set_reach(float p_metres);
	float get_reach() const;

	AerialPerspectiveEffect();
	~AerialPerspectiveEffect();
};
