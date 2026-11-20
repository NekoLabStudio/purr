#include "atmosphere_tables.h"

#include "core/object/callable_mp.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"
#include "servers/rendering/rendering_server.h"

namespace {
const Size2i TRANSMITTANCE_SIZE(256, 64);
const Size2i MULTI_SCATTERING_SIZE(32, 32);
const Size2i SKY_VIEW_SIZE(192, 108);
const Size2i DOME_SIZE(1024, 512);
constexpr int TURNS = 16; // A texel of the dome is worked out every 16 frames.
constexpr int SHAPE_SIZE = 128;
constexpr int DETAIL_SIZE = 32;

RID make_table(const Size2i &p_size) {
	RD::TextureFormat format;
	format.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
	format.width = p_size.x;
	format.height = p_size.y;
	format.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT;
	return RD::get_singleton()->texture_create(format, RD::TextureView());
}
} // namespace

AtmosphereTables *AtmosphereTables::singleton = nullptr;

AtmosphereTables *AtmosphereTables::get_singleton() {
	if (singleton == nullptr) {
		singleton = memnew(AtmosphereTables);
	}
	return singleton;
}

void AtmosphereTables::free_singleton() {
	if (singleton != nullptr) {
		memdelete(singleton);
		singleton = nullptr;
	}
}

AtmosphereTables::AtmosphereTables() {
	sky_view_texture.instantiate();
	transmittance_texture.instantiate();
	dome_texture.instantiate();
}

AtmosphereTables::~AtmosphereTables() {
	sky_view_texture.unref();
	transmittance_texture.unref();
	dome_texture.unref();
	ScreenEffect::release(state);
}

AtmosphereTables::State::~State() {
	for (const RID &rid : { figures_buffer, transmittance, multi_scattering, sky_view, dome, shape_noise, detail_noise, repeating_sampler }) {
		if (rid.is_valid()) {
			RD::get_singleton()->free_rid(rid);
		}
	}
}

bool AtmosphereTables::set_figures(const PackedFloat32Array &p_figures) {
	ERR_FAIL_COND_V(p_figures.size() != FIGURES, false);
	if (p_figures == figures || RS::get_singleton() == nullptr || RD::get_singleton() == nullptr) {
		return false;
	}
	figures = p_figures;
	RS::get_singleton()->call_on_render_thread(callable_mp_static(&AtmosphereTables::_update_on_render_thread).bind(uint64_t(this), figures));
	return true;
}

// A table kernel into its table: the figures at binding 0, the table at 1, what it reads from 2.
void AtmosphereTables::_dispatch_table(RID p_shader, RID p_pipeline, RID p_table, const Size2i &p_size, const LocalVector<RID> &p_reads) {
	LocalVector<RD::Uniform> uniforms;
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, state->figures_buffer));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 1, p_table));
	for (uint32_t i = 0; i < p_reads.size(); i++) {
		uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2 + i, Vector<RID>({ ScreenEffect::linear_sampler(), p_reads[i] })));
	}
	LocalVector<RID> sets;
	sets.push_back(UniformSetCacheRD::get_singleton()->get_cache_vec(p_shader, 0, uniforms));
	ScreenEffect::dispatch(p_pipeline, sets, nullptr, 0, p_size);
}

void AtmosphereTables::_update_on_render_thread(uint64_t p_tables, const PackedFloat32Array &p_figures) {
	AtmosphereTables *tables = reinterpret_cast<AtmosphereTables *>(p_tables);
	if (tables == singleton) { // Not since freed.
		tables->_update(p_figures);
	}
}

void AtmosphereTables::_update(const PackedFloat32Array &p_figures) {
	RenderingDevice *rd = RD::get_singleton();
	if (state == nullptr) {
		state = memnew(State);
	}
	if (!state->transmittance_shader.ensure() || !state->multi_scattering_shader.ensure() || !state->sky_view_shader.ensure()) {
		return;
	}
	if (state->figures_buffer.is_null()) {
		state->figures_buffer = rd->uniform_buffer_create(FIGURES * sizeof(float));
		state->transmittance = make_table(TRANSMITTANCE_SIZE);
		state->multi_scattering = make_table(MULTI_SCATTERING_SIZE);
		state->sky_view = make_table(SKY_VIEW_SIZE);
		transmittance_texture->set_texture_rd_rid(state->transmittance);
		sky_view_texture->set_texture_rd_rid(state->sky_view);
	}
	rd->buffer_update(state->figures_buffer, 0, FIGURES * sizeof(float), p_figures.ptr());
	state->camera_height = p_figures[14];
	// The first two tables depend on the air alone, not the sun or the light on it.
	PackedFloat32Array lasting = p_figures.slice(0, SUN_FIGURES);
	lasting.append_array(p_figures.slice(AMBIENT_FIGURES + 4));
	if (lasting != state->lasting) {
		state->lasting = lasting;
		_dispatch_table(state->transmittance_shader.get_shader(), state->transmittance_shader.get_pipeline(), state->transmittance, TRANSMITTANCE_SIZE, LocalVector<RID>());
		LocalVector<RID> reads;
		reads.push_back(state->transmittance);
		_dispatch_table(state->multi_scattering_shader.get_shader(), state->multi_scattering_shader.get_pipeline(), state->multi_scattering, MULTI_SCATTERING_SIZE, reads);
	}
	LocalVector<RID> reads;
	reads.push_back(state->transmittance);
	reads.push_back(state->multi_scattering);
	_dispatch_table(state->sky_view_shader.get_shader(), state->sky_view_shader.get_pipeline(), state->sky_view, SKY_VIEW_SIZE, reads);
}

void AtmosphereTables::step_clouds(const PackedFloat32Array &p_figures) {
	ERR_FAIL_COND(p_figures.size() != CLOUD_FIGURES);
	if (RS::get_singleton() == nullptr || RD::get_singleton() == nullptr) {
		return;
	}
	RS::get_singleton()->call_on_render_thread(callable_mp_static(&AtmosphereTables::_march_on_render_thread).bind(uint64_t(this), p_figures));
}

void AtmosphereTables::_march_on_render_thread(uint64_t p_tables, const PackedFloat32Array &p_figures) {
	AtmosphereTables *tables = reinterpret_cast<AtmosphereTables *>(p_tables);
	if (tables == singleton) {
		tables->_march(p_figures);
	}
}

// A tiling noise volume, p_size across (p_kind 0 the shape's, 1 the detail's).
RID AtmosphereTables::_make_noise(int p_size, float p_kind, float p_cells) {
	RenderingDevice *rd = RD::get_singleton();
	RD::TextureFormat format;
	format.texture_type = RD::TEXTURE_TYPE_3D;
	format.width = p_size;
	format.height = p_size;
	format.depth = p_size;
	format.format = RD::DATA_FORMAT_R8G8B8A8_UNORM;
	format.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT;
	const RID texture = rd->texture_create(format, RD::TextureView());
	RD::Uniform out(RD::UNIFORM_TYPE_IMAGE, 0, texture);
	const float kind[4] = { p_kind, p_cells, 0.0f, 0.0f };
	RD::ComputeListID list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(list, state->noise_shader.get_pipeline());
	rd->compute_list_bind_uniform_set(list, UniformSetCacheRD::get_singleton()->get_cache(state->noise_shader.get_shader(), 0, out), 0);
	rd->compute_list_set_push_constant(list, kind, sizeof(kind));
	rd->compute_list_dispatch(list, (p_size + 3) / 4, (p_size + 3) / 4, (p_size + 3) / 4);
	rd->compute_list_end();
	return texture;
}

void AtmosphereTables::_march(const PackedFloat32Array &p_figures) {
	RenderingDevice *rd = RD::get_singleton();
	if (state == nullptr || state->figures_buffer.is_null() || !state->noise_shader.ensure() || !state->march_shader.ensure()) {
		return; // The air isn't set yet.
	}
	if (state->dome.is_null()) {
		state->dome = make_table(DOME_SIZE);
		state->shape_noise = _make_noise(SHAPE_SIZE, 0.0, 4.0);
		state->detail_noise = _make_noise(DETAIL_SIZE, 1.0, 4.0);
		RD::SamplerState repeating;
		repeating.min_filter = RD::SAMPLER_FILTER_LINEAR;
		repeating.mag_filter = RD::SAMPLER_FILTER_LINEAR;
		repeating.repeat_u = RD::SAMPLER_REPEAT_MODE_REPEAT;
		repeating.repeat_v = RD::SAMPLER_REPEAT_MODE_REPEAT;
		repeating.repeat_w = RD::SAMPLER_REPEAT_MODE_REPEAT;
		state->repeating_sampler = rd->sampler_create(repeating);
		dome_texture->set_texture_rd_rid(state->dome);
	}
	LocalVector<RD::Uniform> uniforms;
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, state->figures_buffer));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 1, state->dome));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ ScreenEffect::linear_sampler(), state->transmittance })));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 3, Vector<RID>({ ScreenEffect::linear_sampler(), state->multi_scattering })));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 4, Vector<RID>({ ScreenEffect::linear_sampler(), state->sky_view })));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 5, Vector<RID>({ state->repeating_sampler, state->shape_noise })));
	uniforms.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 6, Vector<RID>({ state->repeating_sampler, state->detail_noise })));
	const RID set = UniformSetCacheRD::get_singleton()->get_cache_vec(state->march_shader.get_shader(), 0, uniforms);
	PackedFloat32Array these = p_figures;
	for (int i = 0; i < (state->dome_filled ? 1 : TURNS); i++) {
		these.set(3, float(state->turn));
		state->turn = (state->turn + 1) % TURNS;
		RD::ComputeListID list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(list, state->march_shader.get_pipeline());
		rd->compute_list_bind_uniform_set(list, set, 0);
		rd->compute_list_set_push_constant(list, these.ptr(), CLOUD_FIGURES * sizeof(float));
		rd->compute_list_dispatch(list, (DOME_SIZE.x + 31) / 32, (DOME_SIZE.y + 31) / 32, 1);
		rd->compute_list_end();
	}
	state->dome_filled = true;
}

