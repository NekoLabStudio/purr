#include "color_grade_volume_3d.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/3d/world_3d.h"

#include "sky_globals.h"

LocalVector<ColorGradeVolume3D *> ColorGradeVolume3D::volumes;

namespace {
// The blended lookup table's size along each side.
constexpr int LUT_SIZE = 33;

// The environment as it was before any volume touched it, put back when the last one goes.
struct Grading {
	Ref<Environment> environment;
	bool adjustment_enabled = false;
	float brightness = 1.0;
	float contrast = 1.0;
	float saturation = 1.0;
	float exposure = 1.0;
	Ref<Texture> color_correction;
	Ref<ImageTexture3D> lut; // The blend, when a lookup table or a tint is in it.
	String lut_made_from; // What the blend was last made from, so it's made again only when that changes.
	HashMap<RID, Vector<Color>> resampled; // Each grade's table, at LUT_SIZE.
};
Grading *grading = nullptr;

Color sample(const Vector<Ref<Image>> &p_slices, const Vector3 &p_at) {
	const int depth = p_slices.size();
	const int width = p_slices[0]->get_width();
	const int height = p_slices[0]->get_height();
	const Vector3 scaled = p_at * Vector3(width - 1, height - 1, depth - 1);
	const Vector3i low = Vector3i(Math::floor(scaled.x), Math::floor(scaled.y), Math::floor(scaled.z));
	const Vector3 f = scaled - Vector3(low);
	Color c[2][2][2];
	for (int z = 0; z < 2; z++) {
		for (int y = 0; y < 2; y++) {
			for (int x = 0; x < 2; x++) {
				const int sx = MIN(low.x + x, width - 1);
				const int sy = MIN(low.y + y, height - 1);
				const int sz = MIN(low.z + z, depth - 1);
				c[z][y][x] = p_slices[sz]->get_pixel(sx, sy);
			}
		}
	}
	const Color y0 = c[0][0][0].lerp(c[0][0][1], f.x).lerp(c[0][1][0].lerp(c[0][1][1], f.x), f.y);
	const Color y1 = c[1][0][0].lerp(c[1][0][1], f.x).lerp(c[1][1][0].lerp(c[1][1][1], f.x), f.y);
	return y0.lerp(y1, f.z);
}

// A lookup table at LUT_SIZE: red across, green down, blue through the slices. None is the
// identity, which changes nothing.
const Vector<Color> &resampled(const Ref<Texture3D> &p_lut) {
	static Vector<Color> identity;
	if (p_lut.is_null()) {
		if (identity.is_empty()) {
			identity.resize(LUT_SIZE * LUT_SIZE * LUT_SIZE);
			for (int b = 0; b < LUT_SIZE; b++) {
				for (int g = 0; g < LUT_SIZE; g++) {
					for (int r = 0; r < LUT_SIZE; r++) {
						identity.write[(b * LUT_SIZE + g) * LUT_SIZE + r] = Color(r, g, b) / float(LUT_SIZE - 1);
					}
				}
			}
		}
		return identity;
	}
	Vector<Color> *cached = grading->resampled.getptr(p_lut->get_rid());
	if (cached != nullptr) {
		return *cached;
	}
	Vector<Ref<Image>> slices = p_lut->get_data();
	for (Ref<Image> &slice : slices) {
		if (slice.is_valid() && slice->is_compressed()) {
			slice = slice->duplicate();
			slice->decompress();
		}
	}
	if (slices.is_empty() || slices[0].is_null()) {
		ERR_PRINT_ONCE(vformat("ColorGrade's lookup table %s has no data to read.", p_lut->get_path()));
		return resampled(Ref<Texture3D>());
	}
	Vector<Color> table;
	table.resize(LUT_SIZE * LUT_SIZE * LUT_SIZE);
	for (int b = 0; b < LUT_SIZE; b++) {
		for (int g = 0; g < LUT_SIZE; g++) {
			for (int r = 0; r < LUT_SIZE; r++) {
				table.write[(b * LUT_SIZE + g) * LUT_SIZE + r] = sample(slices, Vector3(r, g, b) / float(LUT_SIZE - 1));
			}
		}
	}
	return grading->resampled.insert(p_lut->get_rid(), table)->value;
}
} // namespace

void ColorGradeVolume3D::cleanup() {
	if (grading != nullptr) {
		memdelete(grading);
		grading = nullptr;
	}
}

void ColorGradeVolume3D::_notification(int p_what) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return; // The scene's environment is saved as it is.
	}
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			volumes.push_back(this);
			set_process_internal(true);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			volumes.erase(this);
			if (volumes.is_empty()) {
				_restore();
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			// Once a frame, by whichever volume comes first.
			if (!volumes.is_empty() && volumes[0] == this) {
				_apply(get_viewport());
			}
		} break;
	}
}

float ColorGradeVolume3D::get_influence(const Vector3 &p_camera) const {
	if (grade.is_null() || !is_visible_in_tree()) {
		return 0.0f;
	}
	float now = 1.0f;
	switch (when) {
		case WHEN_ALWAYS:
			break;
		case WHEN_DAY:
			now = 1.0f - float(SkyGlobals::get(SkyGlobals::NIGHT));
			break;
		case WHEN_NIGHT:
			now = float(SkyGlobals::get(SkyGlobals::NIGHT));
			break;
		case WHEN_DAWN_AND_DUSK: {
			const Vector3 sun = SkyGlobals::get(SkyGlobals::SUN_DIRECTION);
			now = sun.is_zero_approx() ? 0.0f : Math::exp(-(sun.y / 0.15f) * (sun.y / 0.15f));
		} break;
		case WHEN_OVERCAST:
			now = float(SkyGlobals::get(SkyGlobals::OVERCAST));
			break;
		case WHEN_RAINING:
			now = float(SkyGlobals::get(SkyGlobals::RAIN));
			break;
		case WHEN_SNOWING:
			now = float(SkyGlobals::get(SkyGlobals::SNOW));
			break;
	}
	float inside = 1.0f;
	if (!global) {
		// How far outside the box the camera is, fading out over blend_distance.
		const Vector3 local = get_global_transform().affine_inverse().xform(p_camera);
		const Vector3 beyond = (local.abs() - size * 0.5f).max(Vector3());
		inside = 1.0f - Math::smoothstep(0.0f, MAX(blend_distance, 0.001f), beyond.length());
	}
	return CLAMP(weight * now * inside, 0.0f, 1.0f);
}

void ColorGradeVolume3D::_restore() {
	if (grading == nullptr || grading->environment.is_null()) {
		return;
	}
	const Ref<Environment> environment = grading->environment;
	environment->set_adjustment_enabled(grading->adjustment_enabled);
	environment->set_adjustment_brightness(grading->brightness);
	environment->set_adjustment_contrast(grading->contrast);
	environment->set_adjustment_saturation(grading->saturation);
	environment->set_adjustment_color_correction(grading->color_correction);
	environment->set_tonemap_exposure(grading->exposure);
	grading->environment.unref();
	grading->lut_made_from = String();
}

void ColorGradeVolume3D::_capture(const Ref<Environment> &p_environment) {
	if (p_environment.is_null()) {
		return;
	}
	grading->adjustment_enabled = p_environment->is_adjustment_enabled();
	grading->brightness = p_environment->get_adjustment_brightness();
	grading->contrast = p_environment->get_adjustment_contrast();
	grading->saturation = p_environment->get_adjustment_saturation();
	grading->color_correction = p_environment->get_adjustment_color_correction();
	grading->exposure = p_environment->get_tonemap_exposure();
}

void ColorGradeVolume3D::_apply(Viewport *p_viewport) {
	const Camera3D *camera = p_viewport != nullptr ? p_viewport->get_camera_3d() : nullptr;
	if (camera == nullptr) {
		return;
	}
	Ref<Environment> environment = camera->get_environment();
	if (environment.is_null() && camera->get_world_3d().is_valid()) {
		environment = camera->get_world_3d()->get_environment();
	}
	if (grading == nullptr) {
		grading = memnew(Grading);
	}
	if (environment != grading->environment) {
		_restore();
		grading->environment = environment;
		_capture(environment);
	}
	if (environment.is_null()) {
		return;
	}

	// Lower priorities first, each blended over what's below it by how much it applies.
	LocalVector<ColorGradeVolume3D *> ordered;
	ordered.reserve(volumes.size());
	for (ColorGradeVolume3D *volume : volumes) {
		ordered.push_back(volume);
	}
	struct Lower {
		bool operator()(const ColorGradeVolume3D *p_a, const ColorGradeVolume3D *p_b) const { return p_a->priority < p_b->priority; }
	};
	ordered.sort_custom<Lower>();
	const Vector3 eye = camera->get_global_position();
	float brightness = grading->brightness;
	float contrast = grading->contrast;
	float saturation = grading->saturation;
	float exposure = grading->exposure;
	Color tint = Color(1, 1, 1);
	// The tables in the blend and how much of each: the environment's own first.
	const Ref<Texture3D> own_table = grading->color_correction;
	LocalVector<Pair<Ref<Texture3D>, float>> tables;
	tables.push_back(Pair<Ref<Texture3D>, float>(own_table, 1.0f));
	float applied = 0.0f;
	bool needs_table = own_table.is_valid();
	for (const ColorGradeVolume3D *volume : ordered) {
		const float w = volume->get_influence(eye);
		if (w <= 0.0f) {
			continue;
		}
		applied += w;
		const Ref<ColorGrade> &look = volume->grade;
		brightness = Math::lerp(brightness, look->get_brightness(), w);
		contrast = Math::lerp(contrast, look->get_contrast(), w);
		saturation = Math::lerp(saturation, look->get_saturation(), w);
		exposure = Math::lerp(exposure, grading->exposure * look->get_exposure(), w);
		tint = tint.lerp(look->get_tint(), w);
		for (Pair<Ref<Texture3D>, float> &table : tables) {
			table.second *= 1.0f - w;
		}
		tables.push_back(Pair<Ref<Texture3D>, float>(look->get_lut(), w));
		needs_table = needs_table || look->get_lut().is_valid() || !look->get_tint().is_equal_approx(Color(1, 1, 1));
	}
	if (applied <= 0.0f) {
		if (grading->lut_made_from != "own") {
			_restore();
			grading->environment = environment; // Still watched, as it was.
			grading->lut_made_from = "own";
		}
		// With no volume applying, the environment's values are the game's own, which it may change
		// meanwhile (a brightness option, the time of day): the blend starts from them next time.
		_capture(environment);
		return;
	}
	environment->set_adjustment_enabled(true);
	environment->set_adjustment_brightness(brightness);
	environment->set_adjustment_contrast(contrast);
	environment->set_adjustment_saturation(saturation);
	environment->set_tonemap_exposure(exposure);
	if (!needs_table) {
		environment->set_adjustment_color_correction(Ref<Texture>());
		grading->lut_made_from = String();
		return;
	}
	// Made again only when what it's made from has changed visibly.
	String made_from = vformat("%.2f %.2f %.2f", tint.r, tint.g, tint.b);
	for (const Pair<Ref<Texture3D>, float> &table : tables) {
		made_from += vformat(" %d:%.2f", table.first.is_valid() ? int64_t(table.first->get_rid().get_id()) : 0, table.second);
	}
	if (made_from == grading->lut_made_from) {
		return;
	}
	grading->lut_made_from = made_from;
	Vector<Color> blend;
	blend.resize(LUT_SIZE * LUT_SIZE * LUT_SIZE);
	blend.fill(Color(0, 0, 0, 0));
	Color *out = blend.ptrw();
	for (const Pair<Ref<Texture3D>, float> &table : tables) {
		if (table.second <= 0.0f) {
			continue;
		}
		const Color *in = resampled(table.first).ptr();
		for (int i = 0; i < blend.size(); i++) {
			out[i] += in[i] * table.second;
		}
	}
	Vector<Ref<Image>> slices;
	for (int b = 0; b < LUT_SIZE; b++) {
		Ref<Image> slice = Image::create_empty(LUT_SIZE, LUT_SIZE, false, Image::FORMAT_RGBA8);
		for (int g = 0; g < LUT_SIZE; g++) {
			for (int r = 0; r < LUT_SIZE; r++) {
				const Color c = out[(b * LUT_SIZE + g) * LUT_SIZE + r] * tint;
				slice->set_pixel(r, g, Color(CLAMP(c.r, 0.0f, 1.0f), CLAMP(c.g, 0.0f, 1.0f), CLAMP(c.b, 0.0f, 1.0f), 1.0f));
			}
		}
		slices.push_back(slice);
	}
	if (grading->lut.is_null()) {
		grading->lut.instantiate();
		grading->lut->create(Image::FORMAT_RGBA8, LUT_SIZE, LUT_SIZE, LUT_SIZE, false, slices);
	} else {
		grading->lut->update(slices);
	}
	environment->set_adjustment_color_correction(grading->lut);
}

void ColorGradeVolume3D::_validate_property(PropertyInfo &p_property) const {
	// A global volume has no box.
	if (global && (p_property.name == "size" || p_property.name == "blend_distance")) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

void ColorGradeVolume3D::set_grade(const Ref<ColorGrade> &p_grade) {
	grade = p_grade;
}

Ref<ColorGrade> ColorGradeVolume3D::get_grade() const {
	return grade;
}

void ColorGradeVolume3D::set_global(bool p_global) {
	global = p_global;
	notify_property_list_changed();
}

bool ColorGradeVolume3D::is_global() const {
	return global;
}

void ColorGradeVolume3D::set_size(const Vector3 &p_size) {
	size = p_size.max(Vector3());
}

Vector3 ColorGradeVolume3D::get_size() const {
	return size;
}

void ColorGradeVolume3D::set_blend_distance(float p_metres) {
	blend_distance = MAX(p_metres, 0.0f);
}

float ColorGradeVolume3D::get_blend_distance() const {
	return blend_distance;
}

void ColorGradeVolume3D::set_priority(int p_priority) {
	priority = p_priority;
}

int ColorGradeVolume3D::get_priority() const {
	return priority;
}

void ColorGradeVolume3D::set_weight(float p_weight) {
	weight = CLAMP(p_weight, 0.0f, 1.0f);
}

float ColorGradeVolume3D::get_weight() const {
	return weight;
}

void ColorGradeVolume3D::set_when(When p_when) {
	when = p_when;
}

ColorGradeVolume3D::When ColorGradeVolume3D::get_when() const {
	return when;
}

void ColorGradeVolume3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_grade", "grade"), &ColorGradeVolume3D::set_grade);
	ClassDB::bind_method(D_METHOD("get_grade"), &ColorGradeVolume3D::get_grade);
	ClassDB::bind_method(D_METHOD("set_global", "global"), &ColorGradeVolume3D::set_global);
	ClassDB::bind_method(D_METHOD("is_global"), &ColorGradeVolume3D::is_global);
	ClassDB::bind_method(D_METHOD("set_size", "size"), &ColorGradeVolume3D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &ColorGradeVolume3D::get_size);
	ClassDB::bind_method(D_METHOD("set_blend_distance", "metres"), &ColorGradeVolume3D::set_blend_distance);
	ClassDB::bind_method(D_METHOD("get_blend_distance"), &ColorGradeVolume3D::get_blend_distance);
	ClassDB::bind_method(D_METHOD("set_priority", "priority"), &ColorGradeVolume3D::set_priority);
	ClassDB::bind_method(D_METHOD("get_priority"), &ColorGradeVolume3D::get_priority);
	ClassDB::bind_method(D_METHOD("set_weight", "weight"), &ColorGradeVolume3D::set_weight);
	ClassDB::bind_method(D_METHOD("get_weight"), &ColorGradeVolume3D::get_weight);
	ClassDB::bind_method(D_METHOD("set_when", "when"), &ColorGradeVolume3D::set_when);
	ClassDB::bind_method(D_METHOD("get_when"), &ColorGradeVolume3D::get_when);
	ClassDB::bind_method(D_METHOD("get_influence", "camera"), &ColorGradeVolume3D::get_influence);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "grade", PROPERTY_HINT_RESOURCE_TYPE, "ColorGrade"), "set_grade", "get_grade");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "when", PROPERTY_HINT_ENUM, "Always,Day,Night,Dawn and Dusk,Overcast,Raining,Snowing"), "set_when", "get_when");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "global"), "set_global", "is_global");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blend_distance", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater,suffix:m"), "set_blend_distance", "get_blend_distance");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "priority", PROPERTY_HINT_RANGE, "-100,100,1,or_less,or_greater"), "set_priority", "get_priority");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "weight", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_weight", "get_weight");

	BIND_ENUM_CONSTANT(WHEN_ALWAYS);
	BIND_ENUM_CONSTANT(WHEN_DAY);
	BIND_ENUM_CONSTANT(WHEN_NIGHT);
	BIND_ENUM_CONSTANT(WHEN_DAWN_AND_DUSK);
	BIND_ENUM_CONSTANT(WHEN_OVERCAST);
	BIND_ENUM_CONSTANT(WHEN_RAINING);
	BIND_ENUM_CONSTANT(WHEN_SNOWING);
}
