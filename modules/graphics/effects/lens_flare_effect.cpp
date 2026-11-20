#include "lens_flare_effect.h"

#include "core/object/class_db.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

LensFlareEffect::LensFlareEffect() {
	ScreenEffect::setup(this, callable_mp(this, &LensFlareEffect::_draw), true, false);
}

LensFlareEffect::~LensFlareEffect() {
	ScreenEffect::release(state);
}

void LensFlareEffect::_draw(int p_callback_type, const RenderData *p_render_data) {
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
		if (!ScreenEffect::on_screen(p_render_data, view, towards, sun) || sun.x < -0.15 || sun.x > 1.15 || sun.y < -0.15 || sun.y > 1.15) {
			continue;
		}
		struct {
			float size[2];
			float sun[2];
			float tint[4];
			float aspect;
			float glare;
			float ghosts;
			float pad;
		} params = { { float(size.x), float(size.y) }, { sun.x, sun.y }, { tint.r, tint.g, tint.b, 1.0 }, float(size.x) / float(size.y), glare_size, ghosts, 0.0 };
		RD::Uniform image(RD::UNIFORM_TYPE_IMAGE, 0, buffers->get_internal_texture(view));
		RD::Uniform depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ ScreenEffect::linear_sampler(), buffers->get_depth_texture(view) }));
		LocalVector<RID> sets;
		sets.push_back(UniformSetCacheRD::get_singleton()->get_cache(state->shader.get_shader(), 0, image, depth));
		ScreenEffect::dispatch(state->shader.get_pipeline(), sets, &params, sizeof(params), size);
	}
}

void LensFlareEffect::set_strength(float p_strength) {
	strength = MAX(p_strength, 0.0f);
}

float LensFlareEffect::get_strength() const {
	return strength;
}

void LensFlareEffect::set_color(Color p_color) {
	color = p_color;
}

Color LensFlareEffect::get_color() const {
	return color;
}

void LensFlareEffect::set_glare_size(float p_glare_size) {
	glare_size = MAX(p_glare_size, 0.0f);
}

float LensFlareEffect::get_glare_size() const {
	return glare_size;
}

void LensFlareEffect::set_ghosts(float p_ghosts) {
	ghosts = MAX(p_ghosts, 0.0f);
}

float LensFlareEffect::get_ghosts() const {
	return ghosts;
}

void LensFlareEffect::_validate_property(PropertyInfo &p_property) const {
	ScreenEffect::hide_internals(p_property);
}

void LensFlareEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_strength", "strength"), &LensFlareEffect::set_strength);
	ClassDB::bind_method(D_METHOD("get_strength"), &LensFlareEffect::get_strength);
	ClassDB::bind_method(D_METHOD("set_color", "color"), &LensFlareEffect::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &LensFlareEffect::get_color);
	ClassDB::bind_method(D_METHOD("set_glare_size", "glare_size"), &LensFlareEffect::set_glare_size);
	ClassDB::bind_method(D_METHOD("get_glare_size"), &LensFlareEffect::get_glare_size);
	ClassDB::bind_method(D_METHOD("set_ghosts", "ghosts"), &LensFlareEffect::set_ghosts);
	ClassDB::bind_method(D_METHOD("get_ghosts"), &LensFlareEffect::get_ghosts);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strength", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_strength", "get_strength");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "glare_size", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_glare_size", "get_glare_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ghosts", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_ghosts", "get_ghosts");
}
