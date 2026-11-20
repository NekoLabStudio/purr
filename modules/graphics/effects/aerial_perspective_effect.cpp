#include "aerial_perspective_effect.h"

#include "core/object/class_db.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

#include "atmosphere_tables.h"

AerialPerspectiveEffect::State::~State() {
	for (const RID &volume : volumes) {
		if (volume.is_valid()) {
			RD::get_singleton()->free_rid(volume);
		}
	}
}

AerialPerspectiveEffect::AerialPerspectiveEffect() {
	ScreenEffect::setup(this, callable_mp(this, &AerialPerspectiveEffect::_draw), true, false);
}

AerialPerspectiveEffect::~AerialPerspectiveEffect() {
	ScreenEffect::release(state);
}

void AerialPerspectiveEffect::_draw(int p_callback_type, const RenderData *p_render_data) {
	const Ref<RenderSceneBuffersRD> buffers = p_render_data->get_render_scene_buffers();
	AtmosphereTables *tables = AtmosphereTables::get_singleton();
	if (!ScreenEffect::usable(buffers) || !tables->is_ready()) {
		return; // No AtmosphereSkyMaterial has set the air yet.
	}
	if (state == nullptr) {
		state = memnew(State);
	}
	if (!state->volume_shader.ensure() || !state->apply_shader.ensure()) {
		return;
	}
	RenderingDevice *rd = RD::get_singleton();
	const RenderSceneData *scene = p_render_data->get_render_scene_data();
	const Transform3D camera = scene->get_cam_transform();
	const Size2i size = buffers->get_internal_size();
	for (uint32_t view = 0; view < MIN(buffers->get_view_count(), 2u); view++) {
		if (state->volumes[view].is_null()) {
			RD::TextureFormat format;
			format.texture_type = RD::TEXTURE_TYPE_3D;
			format.width = CELLS;
			format.height = CELLS;
			format.depth = CELLS;
			format.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
			format.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT;
			state->volumes[view] = rd->texture_create(format, RD::TextureView());
		}
		const RID volume = state->volumes[view];
		const Projection projection = scene->get_view_projection(view);
		const Projection inverse = projection.inverse();

		// Which way the view's corners look in the world: top left, top right, bottom left, bottom right.
		float fill[20];
		const Vector2 corners[4] = { Vector2(-1, 1), Vector2(1, 1), Vector2(-1, -1), Vector2(1, -1) };
		for (int c = 0; c < 4; c++) {
			const Vector4 at = inverse.xform(Vector4(corners[c].x, corners[c].y, 0.5, 1.0));
			const Vector3 in_view = Vector3(at.x, at.y, at.z) / (Math::abs(at.w) > 1e-8 ? at.w : 1e-8);
			const Vector3 way = camera.basis.xform(in_view);
			fill[c * 4] = way.x;
			fill[c * 4 + 1] = way.y;
			fill[c * 4 + 2] = way.z;
			fill[c * 4 + 3] = 0.0;
		}
		fill[16] = reach / 1000.0;
		fill[17] = tables->get_camera_height(); // Over the ground (km).
		fill[18] = 0.0;
		fill[19] = 0.0;
		{
			LocalVector<RD::Uniform> uniforms;
			uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, tables->get_figures_buffer()));
			uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 1, volume));
			uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ ScreenEffect::linear_sampler(), tables->get_transmittance() })));
			uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 3, Vector<RID>({ ScreenEffect::linear_sampler(), tables->get_multi_scattering() })));
			RD::ComputeListID list = rd->compute_list_begin();
			rd->compute_list_bind_compute_pipeline(list, state->volume_shader.get_pipeline());
			rd->compute_list_bind_uniform_set(list, UniformSetCacheRD::get_singleton()->get_cache_vec(state->volume_shader.get_shader(), 0, uniforms), 0);
			rd->compute_list_set_push_constant(list, fill, sizeof(fill));
			rd->compute_list_dispatch(list, CELLS / 8, CELLS / 8, 1);
			rd->compute_list_end();
		}

		// Laid over the picture by each pixel's distance.
		float apply[20];
		for (int column = 0; column < 4; column++) {
			for (int row = 0; row < 4; row++) {
				apply[column * 4 + row] = inverse.columns[column][row];
			}
		}
		apply[16] = size.x;
		apply[17] = size.y;
		apply[18] = reach / 1000.0;
		apply[19] = CELLS;
		RD::Uniform image(RD::UNIFORM_TYPE_IMAGE, 0, buffers->get_internal_texture(view));
		RD::Uniform depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ ScreenEffect::linear_sampler(), buffers->get_depth_texture(view) }));
		RD::Uniform air(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ ScreenEffect::linear_sampler(), volume }));
		LocalVector<RID> sets;
		sets.push_back(UniformSetCacheRD::get_singleton()->get_cache(state->apply_shader.get_shader(), 0, image, depth, air));
		ScreenEffect::dispatch(state->apply_shader.get_pipeline(), sets, apply, sizeof(apply), size);
	}
}

void AerialPerspectiveEffect::set_reach(float p_metres) {
	reach = MAX(p_metres, 10.0f);
}

float AerialPerspectiveEffect::get_reach() const {
	return reach;
}

void AerialPerspectiveEffect::_validate_property(PropertyInfo &p_property) const {
	ScreenEffect::hide_internals(p_property);
}

void AerialPerspectiveEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_reach", "metres"), &AerialPerspectiveEffect::set_reach);
	ClassDB::bind_method(D_METHOD("get_reach"), &AerialPerspectiveEffect::get_reach);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reach", PROPERTY_HINT_RANGE, "10,100000,1,or_greater,suffix:m"), "set_reach", "get_reach");
}
