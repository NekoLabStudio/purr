#include "light_shafts_effect.h"

#include "core/object/class_db.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

LightShaftsEffect::LightShaftsEffect() {
	ScreenEffect::setup(this, callable_mp(this, &LightShaftsEffect::_draw), true, false);
}

LightShaftsEffect::~LightShaftsEffect() {
	ScreenEffect::release(state);
}

void LightShaftsEffect::_draw(int p_callback_type, const RenderData *p_render_data) {
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
	Vector3 towards;
	Color light;
	if (strength <= 0.0 || !ScreenEffect::find_sun(p_render_data, towards, light)) {
		return;
	}
	const Color tint = light * color * strength;
	for (uint32_t view = 0; view < buffers->get_view_count(); view++) {
		Vector2 sun;
		if (!ScreenEffect::on_screen(p_render_data, view, towards, sun) || sun.x < -spread || sun.x > 1.0 + spread || sun.y < -spread || sun.y > 1.0 + spread) {
			continue;
		}
		struct {
			float size[2];
			float sun[2];
			float tint[4];
			float samples;
			float reach;
			float aspect;
			float spread;
		} params = { { float(size.x), float(size.y) }, { sun.x, sun.y }, { tint.r, tint.g, tint.b, decay }, float(samples), reach, float(size.x) / float(size.y), spread };
		RD::Uniform image(RD::UNIFORM_TYPE_IMAGE, 0, buffers->get_internal_texture(view));
		RD::Uniform depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ ScreenEffect::linear_sampler(), buffers->get_depth_texture(view) }));
		LocalVector<RID> sets;
		sets.push_back(UniformSetCacheRD::get_singleton()->get_cache(state->shader.get_shader(), 0, image, depth));
		ScreenEffect::dispatch(state->shader.get_pipeline(), sets, &params, sizeof(params), size);
	}
}

void LightShaftsEffect::set_strength(float p_strength) {
	strength = MAX(p_strength, 0.0f);
}

float LightShaftsEffect::get_strength() const {
	return strength;
}

void LightShaftsEffect::set_color(Color p_color) {
	color = p_color;
}

Color LightShaftsEffect::get_color() const {
	return color;
}

void LightShaftsEffect::set_decay(float p_decay) {
	decay = CLAMP(p_decay, 0.5f, 1.0f);
}

float LightShaftsEffect::get_decay() const {
	return decay;
}

void LightShaftsEffect::set_samples(int p_samples) {
	samples = CLAMP(p_samples, 4, 128);
}

int LightShaftsEffect::get_samples() const {
	return samples;
}

void LightShaftsEffect::set_reach(float p_reach) {
	reach = CLAMP(p_reach, 0.0f, 1.0f);
}

float LightShaftsEffect::get_reach() const {
	return reach;
}

void LightShaftsEffect::set_spread(float p_spread) {
	spread = MAX(p_spread, 0.0f);
}

float LightShaftsEffect::get_spread() const {
	return spread;
}

void LightShaftsEffect::_validate_property(PropertyInfo &p_property) const {
	ScreenEffect::hide_internals(p_property);
}

void LightShaftsEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_strength", "strength"), &LightShaftsEffect::set_strength);
	ClassDB::bind_method(D_METHOD("get_strength"), &LightShaftsEffect::get_strength);
	ClassDB::bind_method(D_METHOD("set_color", "color"), &LightShaftsEffect::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &LightShaftsEffect::get_color);
	ClassDB::bind_method(D_METHOD("set_decay", "decay"), &LightShaftsEffect::set_decay);
	ClassDB::bind_method(D_METHOD("get_decay"), &LightShaftsEffect::get_decay);
	ClassDB::bind_method(D_METHOD("set_samples", "samples"), &LightShaftsEffect::set_samples);
	ClassDB::bind_method(D_METHOD("get_samples"), &LightShaftsEffect::get_samples);
	ClassDB::bind_method(D_METHOD("set_reach", "reach"), &LightShaftsEffect::set_reach);
	ClassDB::bind_method(D_METHOD("get_reach"), &LightShaftsEffect::get_reach);
	ClassDB::bind_method(D_METHOD("set_spread", "spread"), &LightShaftsEffect::set_spread);
	ClassDB::bind_method(D_METHOD("get_spread"), &LightShaftsEffect::get_spread);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strength", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_strength", "get_strength");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "decay", PROPERTY_HINT_RANGE, "0.5,1,0.001"), "set_decay", "get_decay");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "samples", PROPERTY_HINT_RANGE, "4,128,1"), "set_samples", "get_samples");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reach", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_reach", "get_reach");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spread", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_spread", "get_spread");
}
