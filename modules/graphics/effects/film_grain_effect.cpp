#include "film_grain_effect.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

FilmGrainEffect::FilmGrainEffect() {
	ScreenEffect::setup(this, callable_mp(this, &FilmGrainEffect::_draw), false, false);
}

FilmGrainEffect::~FilmGrainEffect() {
	ScreenEffect::release(state);
}

void FilmGrainEffect::_draw(int p_callback_type, const RenderData *p_render_data) {
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
	if (amount <= 0.0) {
		return;
	}
	struct {
		float size[2];
		float amount;
		float grain_size;
		float seed;
		float coloured;
		float pad[2];
	} params = { { float(size.x), float(size.y) }, amount, grain_size, float(Engine::get_singleton()->get_frames_drawn() % 1024), colored ? 1.0f : 0.0f, { 0, 0 } };
	for (uint32_t view = 0; view < buffers->get_view_count(); view++) {
		RD::Uniform image(RD::UNIFORM_TYPE_IMAGE, 0, buffers->get_internal_texture(view));
		LocalVector<RID> sets;
		sets.push_back(UniformSetCacheRD::get_singleton()->get_cache(state->shader.get_shader(), 0, image));
		ScreenEffect::dispatch(state->shader.get_pipeline(), sets, &params, sizeof(params), size);
	}
}

void FilmGrainEffect::set_amount(float p_amount) {
	amount = MAX(p_amount, 0.0f);
}

float FilmGrainEffect::get_amount() const {
	return amount;
}

void FilmGrainEffect::set_grain_size(float p_grain_size) {
	grain_size = MAX(p_grain_size, 1.0f);
}

float FilmGrainEffect::get_grain_size() const {
	return grain_size;
}

void FilmGrainEffect::set_colored(bool p_colored) {
	colored = p_colored;
}

bool FilmGrainEffect::is_colored() const {
	return colored;
}

void FilmGrainEffect::_validate_property(PropertyInfo &p_property) const {
	ScreenEffect::hide_internals(p_property);
}

void FilmGrainEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_amount", "amount"), &FilmGrainEffect::set_amount);
	ClassDB::bind_method(D_METHOD("get_amount"), &FilmGrainEffect::get_amount);
	ClassDB::bind_method(D_METHOD("set_grain_size", "grain_size"), &FilmGrainEffect::set_grain_size);
	ClassDB::bind_method(D_METHOD("get_grain_size"), &FilmGrainEffect::get_grain_size);
	ClassDB::bind_method(D_METHOD("set_colored", "colored"), &FilmGrainEffect::set_colored);
	ClassDB::bind_method(D_METHOD("is_colored"), &FilmGrainEffect::is_colored);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "amount", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_amount", "get_amount");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grain_size", PROPERTY_HINT_RANGE, "1,4,0.1,suffix:px"), "set_grain_size", "get_grain_size");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "colored"), "set_colored", "is_colored");
}
