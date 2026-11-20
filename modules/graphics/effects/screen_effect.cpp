#include "screen_effect.h"

#include "servers/rendering/renderer_rd/storage_rd/light_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/render_data_rd.h"
#include "servers/rendering/renderer_rd/storage_rd/render_scene_buffers_rd.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/rendering_server_globals.h"

#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

namespace ScreenEffect {

bool find_sun(const RenderData *p_render_data, Vector3 &r_towards, Color &r_light) {
	const RenderDataRD *data = static_cast<const RenderDataRD *>(p_render_data);
	if (data == nullptr || data->lights == nullptr) {
		return false;
	}
	RendererRD::LightStorage *lights = RendererRD::LightStorage::get_singleton();
	float brightest = 0.0;
	for (uint32_t i = 0; i < data->lights->size(); i++) {
		const RID instance = (*data->lights)[i];
		const RID light = lights->light_instance_get_base_light(instance);
		if (lights->light_get_type(light) != RSE::LIGHT_DIRECTIONAL || lights->light_directional_get_sky_mode(light) == RSE::LIGHT_DIRECTIONAL_SKY_MODE_SKY_ONLY) {
			continue;
		}
		const Color colour = lights->light_get_color(light) * lights->light_get_param(light, RSE::LIGHT_PARAM_ENERGY);
		const float brightness = colour.get_luminance();
		if (brightness > brightest) {
			brightest = brightness;
			r_towards = lights->light_instance_get_base_transform(instance).basis.get_column(Vector3::AXIS_Z).normalized();
			r_light = colour;
		}
	}
	return brightest > 0.0;
}

bool on_screen(const RenderData *p_render_data, uint32_t p_view, const Vector3 &p_towards, Vector2 &r_uv) {
	const RenderSceneData *scene = p_render_data->get_render_scene_data();
	const Transform3D camera = scene->get_cam_transform();
	const Vector3 in_view = camera.basis.xform_inv(p_towards);
	if (in_view.z >= 0.0) {
		return false; // Behind the camera.
	}
	const Projection projection = scene->get_view_projection(p_view);
	const Vector4 clip = projection.xform(Vector4(in_view.x, in_view.y, in_view.z, 0.0));
	if (clip.w <= 0.0) {
		return false;
	}
	r_uv = Vector2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
	return true;
}

bool usable(const Ref<RenderSceneBuffersRD> &p_buffers) {
	if (p_buffers.is_null()) {
		return false;
	}
	const Size2i size = p_buffers->get_internal_size();
	if (size.x == 0 || size.y == 0) {
		return false;
	}
	const RID colour = (p_buffers->has_internal_texture() ? p_buffers->get_internal_texture(0) : RID());
	return colour.is_valid() && RD::get_singleton()->texture_get_format(colour).format == RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
}

RID linear_sampler() {
	return RendererRD::MaterialStorage::get_singleton()->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
}

RID ColourCopy::copy(RID p_colour, const Size2i &p_size) {
	RenderingDevice *rd = RD::get_singleton();
	if (!shader.ensure()) {
		return RID();
	}
	if (texture.is_null() || size != p_size) {
		if (texture.is_valid()) {
			rd->free_rid(texture);
		}
		RD::TextureFormat format;
		format.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
		format.width = p_size.x;
		format.height = p_size.y;
		format.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
		texture = rd->texture_create(format, RD::TextureView());
		size = p_size;
	}
	RD::Uniform source(RD::UNIFORM_TYPE_IMAGE, 0, p_colour);
	RD::Uniform target(RD::UNIFORM_TYPE_IMAGE, 1, texture);
	LocalVector<RID> sets;
	sets.push_back(UniformSetCacheRD::get_singleton()->get_cache(shader.get_shader(), 0, source, target));
	dispatch(shader.get_pipeline(), sets, nullptr, 0, p_size);
	return texture;
}

ColourCopy::~ColourCopy() {
	if (texture.is_valid()) {
		RD::get_singleton()->free_rid(texture);
	}
}

void dispatch(RID p_pipeline, const LocalVector<RID> &p_uniform_sets, const void *p_push_constant, uint32_t p_push_size, const Size2i &p_size) {
	RenderingDevice *rd = RD::get_singleton();
	RD::ComputeListID list = rd->compute_list_begin();
	rd->compute_list_bind_compute_pipeline(list, p_pipeline);
	for (uint32_t i = 0; i < p_uniform_sets.size(); i++) {
		rd->compute_list_bind_uniform_set(list, p_uniform_sets[i], i);
	}
	if (p_push_size > 0) {
		rd->compute_list_set_push_constant(list, p_push_constant, p_push_size);
	}
	rd->compute_list_dispatch_threads(list, p_size.x, p_size.y, 1);
	rd->compute_list_end();
}

void setup(CompositorEffect *p_effect, const Callable &p_draw, bool p_needs_depth, bool p_needs_motion_vectors, CompositorEffect::EffectCallbackType p_when, bool p_needs_normal_roughness) {
	p_effect->set_effect_callback_type(p_when);
	p_effect->set_access_resolved_color(true);
	p_effect->set_access_resolved_depth(p_needs_depth);
	p_effect->set_needs_motion_vectors(p_needs_motion_vectors);
	p_effect->set_needs_normal_roughness(p_needs_normal_roughness);
	RS::get_singleton()->compositor_effect_set_callback(p_effect->get_rid(), RSE::CompositorEffectCallbackType(p_when), p_draw);
}

// Set by the effect itself, and not saved: a saved value would replace the effect's own drawing.
void hide_internals(PropertyInfo &p_property) {
	if (p_property.name == "effect_callback_type" || p_property.name == "access_resolved_color" || p_property.name == "access_resolved_depth" ||
			p_property.name == "needs_motion_vectors" || p_property.name == "needs_normal_roughness" || p_property.name == "needs_separate_specular") {
		p_property.usage = PROPERTY_USAGE_NONE;
	}
}

} // namespace ScreenEffect
