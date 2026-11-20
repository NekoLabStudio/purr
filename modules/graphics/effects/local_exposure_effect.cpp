#include "local_exposure_effect.h"

#include "core/object/class_db.h"
#include "servers/rendering/renderer_rd/renderer_scene_render_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/render_data_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server_globals.h"
#include "servers/rendering/storage/camera_attributes_storage.h"

LocalExposureEffect::State::~State() {
	for (const RID &texture : { grid_texture, spare_texture }) {
		if (texture.is_valid()) {
			RD::get_singleton()->free_rid(texture);
		}
	}
}

LocalExposureEffect::LocalExposureEffect() {
	ScreenEffect::setup(this, callable_mp(this, &LocalExposureEffect::_draw), false, false);
}

LocalExposureEffect::~LocalExposureEffect() {
	ScreenEffect::release(state);
}

// A grid of cells, each with BINS bands of brightness and one more for the cell as a whole.
RID LocalExposureEffect::_grid(RID &r_texture, const Vector2i &p_cells) {
	if (r_texture.is_null()) {
		RD::TextureFormat format;
		format.texture_type = RD::TEXTURE_TYPE_3D;
		format.width = p_cells.x;
		format.height = p_cells.y;
		format.depth = BINS + 1;
		format.format = RD::DATA_FORMAT_R16G16_SFLOAT;
		format.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT;
		r_texture = RD::get_singleton()->texture_create(format, RD::TextureView());
	}
	return r_texture;
}

void LocalExposureEffect::_draw(int p_callback_type, const RenderData *p_render_data) {
	const Ref<RenderSceneBuffersRD> buffers = p_render_data->get_render_scene_buffers();
	// Not in a headset: each eye's grid would come from its own picture, and the two would differ.
	if (!ScreenEffect::usable(buffers) || buffers->get_view_count() > 1 || (Math::is_equal_approx(highlights, 1.0f) && Math::is_equal_approx(shadows, 1.0f))) {
		return;
	}
	if (state == nullptr) {
		state = memnew(State);
	}
	if (!state->grid.ensure() || !state->blur.ensure() || !state->apply.ensure()) {
		return;
	}
	const Size2i size = buffers->get_internal_size();
	const Vector2i cells((size.x + CELL - 1) / CELL, (size.y + CELL - 1) / CELL);
	if (cells != state->cells) {
		for (RID *texture : { &state->grid_texture, &state->spare_texture }) {
			if (texture->is_valid()) {
				RD::get_singleton()->free_rid(*texture);
				*texture = RID();
			}
		}
		state->cells = cells;
	}
	const RID grid = _grid(state->grid_texture, cells);
	const RID spare = _grid(state->spare_texture, cells);

	// Middle grey before the exposure: 0.18 after it.
	const RenderDataRD *data = static_cast<const RenderDataRD *>(p_render_data);
	float exposure = 1.0;
	if (data->environment.is_valid()) {
		exposure *= RendererSceneRenderRD::get_singleton()->environment_get_exposure(data->environment);
	}
	if (data->camera_attributes.is_valid()) {
		exposure *= RSG::camera_attributes->camera_attributes_get_exposure_normalization_factor(data->camera_attributes);
	}
	const float middle_grey = 0.18f / MAX(exposure, 1e-6f);

	const RID colour = buffers->get_internal_texture(0);
	const float range[4] = { DARKEST, BRIGHTEST, float(size.x), float(size.y) };
	{
		RD::Uniform image(RD::UNIFORM_TYPE_IMAGE, 0, colour);
		RD::Uniform out(RD::UNIFORM_TYPE_IMAGE, 1, grid);
		RenderingDevice *rd = RD::get_singleton();
		RD::ComputeListID list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(list, state->grid.get_pipeline());
		rd->compute_list_bind_uniform_set(list, UniformSetCacheRD::get_singleton()->get_cache(state->grid.get_shader(), 0, image, out), 0);
		rd->compute_list_set_push_constant(list, range, sizeof(range));
		rd->compute_list_dispatch(list, cells.x, cells.y, 1);
		rd->compute_list_end();
	}
	for (int way = 0; way < 2; way++) {
		const RID from = way == 0 ? grid : spare;
		const RID to = way == 0 ? spare : grid;
		RD::Uniform in(RD::UNIFORM_TYPE_IMAGE, 0, from);
		RD::Uniform out(RD::UNIFORM_TYPE_IMAGE, 1, to);
		const int32_t along[4] = { way == 0 ? 1 : 0, way == 0 ? 0 : 1, 0, 0 };
		RenderingDevice *rd = RD::get_singleton();
		RD::ComputeListID list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(list, state->blur.get_pipeline());
		rd->compute_list_bind_uniform_set(list, UniformSetCacheRD::get_singleton()->get_cache(state->blur.get_shader(), 0, in, out), 0);
		rd->compute_list_set_push_constant(list, along, sizeof(along));
		rd->compute_list_dispatch(list, (cells.x + 7) / 8, (cells.y + 7) / 8, BINS + 1);
		rd->compute_list_end();
	}
	const float params[12] = { DARKEST, BRIGHTEST, float(size.x), float(size.y),
		Math::log2(MAX(middle_grey, 1e-6f)), highlights, shadows, detail,
		float(cells.x), float(cells.y), float(CELL), 0.0f };
	RD::Uniform image(RD::UNIFORM_TYPE_IMAGE, 0, colour);
	RD::Uniform read(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ ScreenEffect::linear_sampler(), grid }));
	LocalVector<RID> sets;
	sets.push_back(UniformSetCacheRD::get_singleton()->get_cache(state->apply.get_shader(), 0, image, read));
	ScreenEffect::dispatch(state->apply.get_pipeline(), sets, params, sizeof(params), size);
}

void LocalExposureEffect::set_highlights(float p_contrast) {
	highlights = CLAMP(p_contrast, 0.0f, 2.0f);
}

float LocalExposureEffect::get_highlights() const {
	return highlights;
}

void LocalExposureEffect::set_shadows(float p_contrast) {
	shadows = CLAMP(p_contrast, 0.0f, 2.0f);
}

float LocalExposureEffect::get_shadows() const {
	return shadows;
}

void LocalExposureEffect::set_detail(float p_detail) {
	detail = CLAMP(p_detail, 0.0f, 1.0f);
}

float LocalExposureEffect::get_detail() const {
	return detail;
}

void LocalExposureEffect::_validate_property(PropertyInfo &p_property) const {
	ScreenEffect::hide_internals(p_property);
}

void LocalExposureEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_highlights", "contrast"), &LocalExposureEffect::set_highlights);
	ClassDB::bind_method(D_METHOD("get_highlights"), &LocalExposureEffect::get_highlights);
	ClassDB::bind_method(D_METHOD("set_shadows", "contrast"), &LocalExposureEffect::set_shadows);
	ClassDB::bind_method(D_METHOD("get_shadows"), &LocalExposureEffect::get_shadows);
	ClassDB::bind_method(D_METHOD("set_detail", "detail"), &LocalExposureEffect::set_detail);
	ClassDB::bind_method(D_METHOD("get_detail"), &LocalExposureEffect::get_detail);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "highlights", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_highlights", "get_highlights");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shadows", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_shadows", "get_shadows");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "detail", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_detail", "get_detail");
}
