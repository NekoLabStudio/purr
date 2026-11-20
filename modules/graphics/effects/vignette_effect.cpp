#include "vignette_effect.h"

#include "core/object/class_db.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

VignetteEffect::VignetteEffect() {
	ScreenEffect::setup(this, callable_mp(this, &VignetteEffect::_draw), false, false);
}

VignetteEffect::~VignetteEffect() {
	ScreenEffect::release(state);
}

void VignetteEffect::_draw(int p_callback_type, const RenderData *p_render_data) {
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
	const Color linear = color.srgb_to_linear();
	struct {
		float size[2];
		float strength;
		float softness;
		float colour[4];
		float aspect;
		float pad[3];
	} params = { { float(size.x), float(size.y) }, strength, MAX(softness, 0.01f), { linear.r, linear.g, linear.b, 1.0 }, float(size.x) / float(size.y), { 0, 0, 0 } };
	for (uint32_t view = 0; view < buffers->get_view_count(); view++) {
		RD::Uniform image(RD::UNIFORM_TYPE_IMAGE, 0, buffers->get_internal_texture(view));
		LocalVector<RID> sets;
		sets.push_back(UniformSetCacheRD::get_singleton()->get_cache(state->shader.get_shader(), 0, image));
		ScreenEffect::dispatch(state->shader.get_pipeline(), sets, &params, sizeof(params), size);
	}
}

void VignetteEffect::set_strength(float p_strength) {
	strength = CLAMP(p_strength, 0.0f, 1.0f);
}

float VignetteEffect::get_strength() const {
	return strength;
}

void VignetteEffect::set_softness(float p_softness) {
	softness = CLAMP(p_softness, 0.0f, 1.0f);
}

float VignetteEffect::get_softness() const {
	return softness;
}

void VignetteEffect::set_color(Color p_color) {
	color = p_color;
}

Color VignetteEffect::get_color() const {
	return color;
}

void VignetteEffect::_validate_property(PropertyInfo &p_property) const {
	ScreenEffect::hide_internals(p_property);
}

void VignetteEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_strength", "strength"), &VignetteEffect::set_strength);
	ClassDB::bind_method(D_METHOD("get_strength"), &VignetteEffect::get_strength);
	ClassDB::bind_method(D_METHOD("set_softness", "softness"), &VignetteEffect::set_softness);
	ClassDB::bind_method(D_METHOD("get_softness"), &VignetteEffect::get_softness);
	ClassDB::bind_method(D_METHOD("set_color", "color"), &VignetteEffect::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &VignetteEffect::get_color);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strength", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_strength", "get_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "softness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_softness", "get_softness");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_color", "get_color");
}
