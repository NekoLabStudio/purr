#pragma once

// What Purr's screen effects share: they're compositor effects drawn after the scene's transparent
// pass (or before it, for those that change surfaces), before tonemapping, each a compute shader over every view's colour (both eyes in VR).
// ScreenEffect sets an effect up and calls its draw() on the render thread; ScreenShader holds one
// compute shader and its pipeline, made there when first needed.

#include "scene/resources/compositor.h"
#include "servers/rendering/renderer_rd/shader_rd.h"
#include "core/object/callable_mp.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/storage/render_data.h"

#include "screen_copy.glsl.gen.h"

class RenderSceneBuffersRD;

template <typename T>
class ScreenShader {
	T shader;
	RID version;
	RID pipeline;
	bool tried = false; // Made once: a shader that fails isn't tried again each frame.

public:
	bool ensure() {
		if (tried) {
			return pipeline.is_valid();
		}
		if (RD::get_singleton() == nullptr) {
			return false;
		}
		tried = true;
		shader.initialize({ "" });
		version = shader.version_create();
		pipeline = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(version, 0));
		return pipeline.is_valid();
	}
	RID get_shader() { return shader.version_get_shader(version, 0); }
	RID get_pipeline() const { return pipeline; }
	~ScreenShader() {
		if (version.is_valid()) {
			if (pipeline.is_valid()) {
				RD::get_singleton()->free_rid(pipeline);
			}
			shader.version_free(version);
		}
	}
};

namespace ScreenEffect {

// The sun: the brightest directional light that lights the scene, with where it shines from
// (towards it) and its colour times its energy.
bool find_sun(const RenderData *p_render_data, Vector3 &r_towards, Color &r_light);
// Where a direction is on a view's screen (0 to 1, perhaps beyond), if it's in front.
bool on_screen(const RenderData *p_render_data, uint32_t p_view, const Vector3 &p_towards, Vector2 &r_uv);
// The colour buffers effects can draw on: half-float RGBA (Forward+), with a size.
bool usable(const Ref<RenderSceneBuffersRD> &p_buffers);
RID linear_sampler();
// A copy of a view's colour as drawn, for effects that read other pixels than the one they write.
struct ColourCopy {
	ScreenShader<ScreenCopyShaderRD> shader;
	RID texture;
	Size2i size;
	RID copy(RID p_colour, const Size2i &p_size);
	~ColourCopy();
};
void dispatch(RID p_pipeline, const LocalVector<RID> &p_uniform_sets, const void *p_push_constant, uint32_t p_push_size, const Size2i &p_size);
// Frees an effect's render-thread state there, once the effect itself is gone.
template <typename State>
void release_now(uint64_t p_state) {
	memdelete(reinterpret_cast<State *>(p_state));
}
template <typename State>
void release(State *p_state) {
	if (p_state == nullptr) {
		return;
	}
	if (RS::get_singleton() != nullptr && RD::get_singleton() != nullptr) {
		RS::get_singleton()->call_on_render_thread(callable_mp_static(&release_now<State>).bind(uint64_t(p_state)));
	}
}
// Sets up an effect: drawn after transparent things unless it says otherwise, with its own draw()
// called on the render thread, and the compositor's settings an effect needn't show hidden.
void setup(CompositorEffect *p_effect, const Callable &p_draw, bool p_needs_depth, bool p_needs_motion_vectors,
		CompositorEffect::EffectCallbackType p_when = CompositorEffect::EFFECT_CALLBACK_TYPE_POST_TRANSPARENT, bool p_needs_normal_roughness = false);
void hide_internals(PropertyInfo &p_property);

} // namespace ScreenEffect
