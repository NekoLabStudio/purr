#include "weather_shelter_texture.h"

#include "screen_effect.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/particles_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

#include "modules/graphics/sky_globals.h"
#include "screen_shelter_copy.glsl.gen.h"

#include <atomic>

namespace WeatherShelterTexture {

namespace {
struct State {
	ScreenShader<ScreenShelterCopyShaderRD> shader;
	RID target; // The RenderingDevice's texture.
	RID texture; // The renderer's, over it, for shaders.
	Size2i size;
	~State() {
		if (texture.is_valid()) {
			RS::get_singleton()->free_rid(texture);
		}
		if (target.is_valid()) {
			RD::get_singleton()->free_rid(target);
		}
	}
};
State *state = nullptr; // The render thread's.
std::atomic<bool> copied = false;
} // namespace

void copy(RID p_collision) {
	RenderingDevice *rd = RD::get_singleton();
	RendererRD::ParticlesStorage *particles = RendererRD::ParticlesStorage::get_singleton();
	if (rd == nullptr || particles == nullptr) {
		return;
	}
	const RID heights = particles->particles_collision_get_heightfield_texture(p_collision);
	if (heights.is_null()) {
		return; // Not drawn yet.
	}
	if (state == nullptr) {
		state = memnew(State);
	}
	if (!state->shader.ensure()) {
		return;
	}
	const RD::TextureFormat from = rd->texture_get_format(heights);
	const Size2i size(from.width, from.height);
	if (state->target.is_null() || state->size != size) {
		if (state->texture.is_valid()) {
			RS::get_singleton()->free_rid(state->texture);
		}
		if (state->target.is_valid()) {
			rd->free_rid(state->target);
		}
		RD::TextureFormat format;
		format.format = RD::DATA_FORMAT_R32_SFLOAT;
		format.width = size.x;
		format.height = size.y;
		format.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
		state->target = rd->texture_create(format, RD::TextureView());
		state->size = size;
		// This is the render thread: the renderer takes the call at once.
		state->texture = RS::get_singleton()->texture_rd_create(state->target);
		RS::get_singleton()->global_shader_parameter_set(SkyGlobals::SHELTER, state->texture);
	}
	const RID nearest = RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RD::Uniform source(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 0, Vector<RID>({ nearest, heights }));
	RD::Uniform target(RD::UNIFORM_TYPE_IMAGE, 1, state->target);
	LocalVector<RID> sets;
	sets.push_back(UniformSetCacheRD::get_singleton()->get_cache(state->shader.get_shader(), 0, source, target));
	const int32_t params[4] = { size.x, size.y, 0, 0 };
	ScreenEffect::dispatch(state->shader.get_pipeline(), sets, params, sizeof(params), size);
	copied = true;
}

bool has_texture() {
	return copied;
}

void cleanup() {
	copied = false;
	ScreenEffect::release(state);
	state = nullptr;
}

} // namespace WeatherShelterTexture
