#include "wet_surfaces_effect.h"

#include "core/object/class_db.h"
#include "servers/rendering/renderer_compositor.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/particles_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server_globals.h"

#include "modules/graphics/sky_globals.h"
#include "modules/graphics/weather_shelter.h"

namespace {
struct Params {
	float inverse_projection[16];
	float projection[16];
	float view_to_world[16];
	float world_to_shelter[16];
	float shelter_extents[4];
	float weather[4];
	float screen[4];
};

void store(float *r_to, const Projection &p_from) {
	for (int column = 0; column < 4; column++) {
		for (int row = 0; row < 4; row++) {
			r_to[column * 4 + row] = p_from.columns[column][row];
		}
	}
}

void store(float *r_to, const Transform3D &p_from) {
	store(r_to, Projection(p_from));
}

float global(const char *p_name) {
	const Variant value = SkyGlobals::get(p_name);
	return value.get_type() == Variant::FLOAT ? float(value) : 0.0f;
}
} // namespace

WetSurfacesEffect::State::~State() {
	for (const RID &buffer : params) {
		if (buffer.is_valid()) {
			RD::get_singleton()->free_rid(buffer);
		}
	}
	if (no_shelter.is_valid()) {
		RD::get_singleton()->free_rid(no_shelter);
	}
}

WetSurfacesEffect::WetSurfacesEffect() {
	SkyGlobals::ensure();
	ScreenEffect::setup(this, callable_mp(this, &WetSurfacesEffect::_draw), true, false, EFFECT_CALLBACK_TYPE_POST_SKY, true);
}

WetSurfacesEffect::~WetSurfacesEffect() {
	ScreenEffect::release(state);
}

void WetSurfacesEffect::_draw(int p_callback_type, const RenderData *p_render_data) {
	const Ref<RenderSceneBuffersRD> buffers = p_render_data->get_render_scene_buffers();
	if (!ScreenEffect::usable(buffers) || !buffers->has_texture(SNAME("forward_clustered"), SNAME("normal_roughness"))) {
		return; // Forward+ only: the Mobile renderer keeps no normals.
	}
	const float wetness = global(SkyGlobals::WETNESS);
	if (wetness <= 0.001f) {
		return;
	}
	if (state == nullptr) {
		state = memnew(State);
	}
	if (!state->shader.ensure()) {
		return;
	}
	RenderingDevice *rd = RD::get_singleton();
	if (state->no_shelter.is_null()) {
		RD::TextureFormat format;
		format.format = RD::DATA_FORMAT_R32_SFLOAT;
		format.width = 1;
		format.height = 1;
		format.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT;
		Vector<uint8_t> nothing;
		nothing.resize(4);
		memset(nothing.ptrw(), 0, 4);
		state->no_shelter = rd->texture_create(format, RD::TextureView(), { nothing });
	}

	// Where the sky is open, from Weather3D's height map, if there's one now.
	const WeatherShelter::Shelter shelter = WeatherShelter::get();
	RID heights;
	if (shelter.collision.is_valid()) {
		heights = RendererRD::ParticlesStorage::get_singleton()->particles_collision_get_heightfield_texture(shelter.collision);
	}

	const RenderSceneData *scene = p_render_data->get_render_scene_data();
	const Transform3D camera = scene->get_cam_transform();
	const Size2i size = buffers->get_internal_size();
	const RID nearest = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	for (uint32_t view = 0; view < MIN(buffers->get_view_count(), 2u); view++) {
		Params params;
		const Projection projection = scene->get_view_projection(view);
		store(params.inverse_projection, projection.inverse());
		store(params.projection, projection);
		store(params.view_to_world, camera * Transform3D(Basis(), scene->get_view_eye_offset(view)));
		store(params.world_to_shelter, heights.is_valid() ? shelter.transform.affine_inverse() : Transform3D());
		params.shelter_extents[0] = shelter.extents.x;
		params.shelter_extents[1] = shelter.extents.y;
		params.shelter_extents[2] = shelter.extents.z;
		params.shelter_extents[3] = heights.is_valid() ? 1.0f : 0.0f;
		params.weather[0] = wetness;
		params.weather[1] = global(SkyGlobals::RAIN);
		params.weather[2] = float(Math::fmod(RSG::rasterizer->get_total_time(), 3600.0));
		params.weather[3] = puddles;
		params.screen[0] = size.x;
		params.screen[1] = size.y;
		params.screen[2] = darkening;
		params.screen[3] = reflections;
		if (state->params[view].is_null()) {
			state->params[view] = rd->uniform_buffer_create(sizeof(Params));
		}
		rd->buffer_update(state->params[view], 0, sizeof(Params), &params);

		const RID colour = buffers->get_internal_texture(view);
		const RID copy = state->copy.copy(colour, size);
		if (copy.is_null()) {
			return;
		}
		RD::Uniform image(RD::UNIFORM_TYPE_IMAGE, 0, colour);
		RD::Uniform drawn(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ nearest, copy }));
		RD::Uniform depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ nearest, buffers->get_depth_texture(view) }));
		RD::Uniform normals(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 3, Vector<RID>({ nearest, buffers->get_texture_slice(SNAME("forward_clustered"), SNAME("normal_roughness"), view, 0) }));
		RD::Uniform overhead(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 4, Vector<RID>({ nearest, heights.is_valid() ? heights : state->no_shelter }));
		RD::Uniform figures(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 5, state->params[view]);
		LocalVector<RID> sets;
		sets.push_back(UniformSetCacheRD::get_singleton()->get_cache(state->shader.get_shader(), 0, image, drawn, depth, normals, overhead, figures));
		ScreenEffect::dispatch(state->shader.get_pipeline(), sets, nullptr, 0, size);
	}
}

void WetSurfacesEffect::set_darkening(float p_darkening) {
	darkening = CLAMP(p_darkening, 0.0f, 1.0f);
}

float WetSurfacesEffect::get_darkening() const {
	return darkening;
}

void WetSurfacesEffect::set_puddles(float p_puddles) {
	puddles = CLAMP(p_puddles, 0.0f, 1.0f);
}

float WetSurfacesEffect::get_puddles() const {
	return puddles;
}

void WetSurfacesEffect::set_reflections(float p_reflections) {
	reflections = CLAMP(p_reflections, 0.0f, 1.0f);
}

float WetSurfacesEffect::get_reflections() const {
	return reflections;
}

void WetSurfacesEffect::_validate_property(PropertyInfo &p_property) const {
	ScreenEffect::hide_internals(p_property);
}

void WetSurfacesEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_darkening", "darkening"), &WetSurfacesEffect::set_darkening);
	ClassDB::bind_method(D_METHOD("get_darkening"), &WetSurfacesEffect::get_darkening);
	ClassDB::bind_method(D_METHOD("set_puddles", "puddles"), &WetSurfacesEffect::set_puddles);
	ClassDB::bind_method(D_METHOD("get_puddles"), &WetSurfacesEffect::get_puddles);
	ClassDB::bind_method(D_METHOD("set_reflections", "reflections"), &WetSurfacesEffect::set_reflections);
	ClassDB::bind_method(D_METHOD("get_reflections"), &WetSurfacesEffect::get_reflections);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "darkening", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_darkening", "get_darkening");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "puddles", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_puddles", "get_puddles");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reflections", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_reflections", "get_reflections");
}
