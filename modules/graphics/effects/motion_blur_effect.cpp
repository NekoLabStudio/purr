#include "motion_blur_effect.h"

#include "core/object/class_db.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

MotionBlurEffect::MotionBlurEffect() {
	ScreenEffect::setup(this, callable_mp(this, &MotionBlurEffect::_draw), false, true);
}

MotionBlurEffect::~MotionBlurEffect() {
	ScreenEffect::release(state);
}

void MotionBlurEffect::_draw(int p_callback_type, const RenderData *p_render_data) {
	const Ref<RenderSceneBuffersRD> buffers = p_render_data->get_render_scene_buffers();
	if (!ScreenEffect::usable(buffers) || shutter <= 0.0) {
		return;
	}
	if (state == nullptr) {
		state = memnew(State);
	}
	if (!state->shader.ensure()) {
		return;
	}
	const Size2i size = buffers->get_internal_size();
	struct {
		float size[2];
		float shutter;
		float samples;
		float longest;
		float pad[3];
	} params = { { float(size.x), float(size.y) }, shutter, float(samples), max_length, { 0, 0, 0 } };
	for (uint32_t view = 0; view < buffers->get_view_count(); view++) {
		const RID colour = buffers->get_internal_texture(view);
		const RID velocity = (buffers->has_velocity_buffer(false) ? buffers->get_velocity_buffer(false, view) : RID());
		if (velocity.is_null()) {
			return;
		}
		const RID drawn = state->copy.copy(colour, size);
		RD::Uniform image(RD::UNIFORM_TYPE_IMAGE, 0, colour);
		RD::Uniform source(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ ScreenEffect::linear_sampler(), drawn }));
		RD::Uniform moved(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ ScreenEffect::linear_sampler(), velocity }));
		LocalVector<RID> sets;
		sets.push_back(UniformSetCacheRD::get_singleton()->get_cache(state->shader.get_shader(), 0, image, source, moved));
		ScreenEffect::dispatch(state->shader.get_pipeline(), sets, &params, sizeof(params), size);
	}
}

void MotionBlurEffect::set_shutter(float p_shutter) {
	shutter = CLAMP(p_shutter, 0.0f, 1.0f);
}

float MotionBlurEffect::get_shutter() const {
	return shutter;
}

void MotionBlurEffect::set_samples(int p_samples) {
	samples = CLAMP(p_samples, 2, 32);
}

int MotionBlurEffect::get_samples() const {
	return samples;
}

void MotionBlurEffect::set_max_length(float p_length) {
	max_length = MAX(p_length, 0.0f);
}

float MotionBlurEffect::get_max_length() const {
	return max_length;
}

void MotionBlurEffect::_validate_property(PropertyInfo &p_property) const {
	ScreenEffect::hide_internals(p_property);
}

void MotionBlurEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_shutter", "shutter"), &MotionBlurEffect::set_shutter);
	ClassDB::bind_method(D_METHOD("get_shutter"), &MotionBlurEffect::get_shutter);
	ClassDB::bind_method(D_METHOD("set_samples", "samples"), &MotionBlurEffect::set_samples);
	ClassDB::bind_method(D_METHOD("get_samples"), &MotionBlurEffect::get_samples);
	ClassDB::bind_method(D_METHOD("set_max_length", "length"), &MotionBlurEffect::set_max_length);
	ClassDB::bind_method(D_METHOD("get_max_length"), &MotionBlurEffect::get_max_length);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shutter", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_shutter", "get_shutter");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "samples", PROPERTY_HINT_RANGE, "2,32,1"), "set_samples", "get_samples");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_length", PROPERTY_HINT_RANGE, "0,0.2,0.001"), "set_max_length", "get_max_length");
}
