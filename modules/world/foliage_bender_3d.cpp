#include "foliage_bender_3d.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"
#include "servers/rendering/rendering_server.h"

LocalVector<FoliageBender3D *> FoliageBender3D::benders;
Ref<ImageTexture> FoliageBender3D::texture;
PackedByteArray FoliageBender3D::last;

namespace {
Ref<Image> make_image(const PackedByteArray &p_data) {
	return Image::create_from_data(FoliageBender3D::MAX_BENDERS * 2, 1, false, Image::FORMAT_RGBAF, p_data);
}
} // namespace

void FoliageBender3D::ensure_global() {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (rs == nullptr || texture.is_valid()) {
		return;
	}
	last.resize(MAX_BENDERS * 2 * 4 * sizeof(float));
	last.fill(0);
	texture = ImageTexture::create_from_image(make_image(last));
	// The renderer won't list its globals outside the editor: a project's own are its settings.
	if (!ProjectSettings::get_singleton()->has_setting(String("shader_globals/") + SHADER_GLOBAL)) {
		rs->global_shader_parameter_add(SHADER_GLOBAL, RSE::GLOBAL_VAR_TYPE_SAMPLER2D, texture);
	} else {
		rs->global_shader_parameter_set(SHADER_GLOBAL, texture);
	}
}

void FoliageBender3D::cleanup() {
	texture.unref();
	last.clear();
}

void FoliageBender3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			ensure_global();
			benders.push_back(this);
			set_process_internal(true);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			benders.erase(this);
			if (benders.is_empty()) {
				_publish(nullptr); // None left: nothing bends.
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			// Once a frame, by whichever bender comes first.
			if (!benders.is_empty() && benders[0] == this) {
				_publish(get_viewport());
			}
		} break;
	}
}

// The benders nearest the camera, written into the texture when they've moved.
void FoliageBender3D::_publish(Viewport *p_viewport) {
	if (texture.is_null()) {
		return;
	}
	const Camera3D *camera = p_viewport != nullptr ? p_viewport->get_camera_3d() : nullptr;
	const Vector3 eye = camera != nullptr ? camera->get_global_position() : Vector3();
	LocalVector<Pair<float, FoliageBender3D *>> nearest;
	for (FoliageBender3D *bender : benders) {
		if (bender->is_visible_in_tree() && bender->strength > 0.0f) {
			nearest.push_back(Pair<float, FoliageBender3D *>(bender->get_global_position().distance_squared_to(eye), bender));
		}
	}
	struct Nearer {
		bool operator()(const Pair<float, FoliageBender3D *> &p_a, const Pair<float, FoliageBender3D *> &p_b) const { return p_a.first < p_b.first; }
	};
	nearest.sort_custom<Nearer>();
	PackedByteArray data;
	data.resize(last.size());
	data.fill(0);
	float *texels = reinterpret_cast<float *>(data.ptrw());
	for (uint32_t i = 0; i < MIN(nearest.size(), uint32_t(MAX_BENDERS)); i++) {
		const FoliageBender3D *bender = nearest[i].second;
		const Vector3 at = bender->get_global_position();
		float *texel = texels + i * 8;
		texel[0] = at.x;
		texel[1] = at.y;
		texel[2] = at.z;
		texel[3] = bender->radius;
		texel[4] = bender->strength;
	}
	if (data == last) {
		return;
	}
	last = data;
	texture->update(make_image(data));
}

void FoliageBender3D::set_radius(float p_radius) {
	radius = MAX(p_radius, 0.01f);
}

float FoliageBender3D::get_radius() const {
	return radius;
}

void FoliageBender3D::set_strength(float p_strength) {
	strength = CLAMP(p_strength, 0.0f, 2.0f);
}

float FoliageBender3D::get_strength() const {
	return strength;
}

void FoliageBender3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &FoliageBender3D::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &FoliageBender3D::get_radius);
	ClassDB::bind_method(D_METHOD("set_strength", "strength"), &FoliageBender3D::set_strength);
	ClassDB::bind_method(D_METHOD("get_strength"), &FoliageBender3D::get_strength);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.01,10,0.01,or_greater,suffix:m"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strength", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_strength", "get_strength");
}
