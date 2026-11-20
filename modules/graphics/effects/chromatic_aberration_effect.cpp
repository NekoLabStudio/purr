#include "chromatic_aberration_effect.h"

#include "core/object/class_db.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

ChromaticAberrationEffect::ChromaticAberrationEffect() {
	ScreenEffect::setup(this, callable_mp(this, &ChromaticAberrationEffect::_draw), false, false);
}

ChromaticAberrationEffect::~ChromaticAberrationEffect() {
	ScreenEffect::release(state);
}

void ChromaticAberrationEffect::_draw(int p_callback_type, const RenderData *p_render_data) {
	const Ref<RenderSceneBuffersRD> buffers = p_render_data->get_render_scene_buffers();
	if (!ScreenEffect::usable(buffers)) {
		return;
	}
	if (state == nullptr) {
		state = memnew(State);
	}
	if (!state->shader.ensure()) {
		return;
	}
	const Size2i size = buffers->get_internal_size();
	if (strength <= 0.0) {
		return;
	}
	struct {
		float size[2];
		float strength;
		float pad;
	} params = { { float(size.x), float(size.y) }, strength, 0.0 };
	for (uint32_t view = 0; view < buffers->get_view_count(); view++) {
		const RID colour = buffers->get_internal_texture(view);
		const RID drawn = state->copy.copy(colour, size);
		RD::Uniform image(RD::UNIFORM_TYPE_IMAGE, 0, colour);
		RD::Uniform source(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ ScreenEffect::linear_sampler(), drawn }));
		LocalVector<RID> sets;
		sets.push_back(UniformSetCacheRD::get_singleton()->get_cache(state->shader.get_shader(), 0, image, source));
		ScreenEffect::dispatch(state->shader.get_pipeline(), sets, &params, sizeof(params), size);
	}
}

void ChromaticAberrationEffect::set_strength(float p_strength) {
	strength = MAX(p_strength, 0.0f);
}

float ChromaticAberrationEffect::get_strength() const {
	return strength;
}

void ChromaticAberrationEffect::_validate_property(PropertyInfo &p_property) const {
	ScreenEffect::hide_internals(p_property);
}

void ChromaticAberrationEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_strength", "strength"), &ChromaticAberrationEffect::set_strength);
	ClassDB::bind_method(D_METHOD("get_strength"), &ChromaticAberrationEffect::get_strength);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strength", PROPERTY_HINT_RANGE, "0,0.1,0.001"), "set_strength", "get_strength");
}
