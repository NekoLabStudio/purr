#include "color_grade.h"

#include "core/object/class_db.h"

void ColorGrade::set_exposure(float p_exposure) {
	exposure = MAX(p_exposure, 0.0f);
	emit_changed();
}

float ColorGrade::get_exposure() const {
	return exposure;
}

void ColorGrade::set_brightness(float p_brightness) {
	brightness = MAX(p_brightness, 0.0f);
	emit_changed();
}

float ColorGrade::get_brightness() const {
	return brightness;
}

void ColorGrade::set_contrast(float p_contrast) {
	contrast = MAX(p_contrast, 0.0f);
	emit_changed();
}

float ColorGrade::get_contrast() const {
	return contrast;
}

void ColorGrade::set_saturation(float p_saturation) {
	saturation = MAX(p_saturation, 0.0f);
	emit_changed();
}

float ColorGrade::get_saturation() const {
	return saturation;
}

void ColorGrade::set_tint(const Color &p_tint) {
	tint = p_tint;
	emit_changed();
}

Color ColorGrade::get_tint() const {
	return tint;
}

void ColorGrade::set_lut(const Ref<Texture3D> &p_lut) {
	lut = p_lut;
	emit_changed();
}

Ref<Texture3D> ColorGrade::get_lut() const {
	return lut;
}

void ColorGrade::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_exposure", "exposure"), &ColorGrade::set_exposure);
	ClassDB::bind_method(D_METHOD("get_exposure"), &ColorGrade::get_exposure);
	ClassDB::bind_method(D_METHOD("set_brightness", "brightness"), &ColorGrade::set_brightness);
	ClassDB::bind_method(D_METHOD("get_brightness"), &ColorGrade::get_brightness);
	ClassDB::bind_method(D_METHOD("set_contrast", "contrast"), &ColorGrade::set_contrast);
	ClassDB::bind_method(D_METHOD("get_contrast"), &ColorGrade::get_contrast);
	ClassDB::bind_method(D_METHOD("set_saturation", "saturation"), &ColorGrade::set_saturation);
	ClassDB::bind_method(D_METHOD("get_saturation"), &ColorGrade::get_saturation);
	ClassDB::bind_method(D_METHOD("set_tint", "tint"), &ColorGrade::set_tint);
	ClassDB::bind_method(D_METHOD("get_tint"), &ColorGrade::get_tint);
	ClassDB::bind_method(D_METHOD("set_lut", "lut"), &ColorGrade::set_lut);
	ClassDB::bind_method(D_METHOD("get_lut"), &ColorGrade::get_lut);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "exposure", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_exposure", "get_exposure");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "brightness", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_brightness", "get_brightness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "contrast", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_contrast", "get_contrast");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "saturation", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_saturation", "get_saturation");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "tint", PROPERTY_HINT_COLOR_NO_ALPHA), "set_tint", "get_tint");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "lut", PROPERTY_HINT_RESOURCE_TYPE, "Texture3D"), "set_lut", "get_lut");
}
