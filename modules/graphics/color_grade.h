#pragma once

// ColorGrade: a look for the picture (exposure, brightness, contrast, saturation, a tint and a
// colour lookup table), which ColorGradeVolume3Ds blend into the environment's own as the camera
// moves between places, the day turns or the weather changes.

#include "core/io/resource.h"
#include "scene/resources/texture.h"

class ColorGrade : public Resource {
	GDCLASS(ColorGrade, Resource);

	float exposure = 1.0;
	float brightness = 1.0;
	float contrast = 1.0;
	float saturation = 1.0;
	Color tint = Color(1, 1, 1);
	Ref<Texture3D> lut;

protected:
	static void _bind_methods();

public:
	void set_exposure(float p_exposure);
	float get_exposure() const;
	void set_brightness(float p_brightness);
	float get_brightness() const;
	void set_contrast(float p_contrast);
	float get_contrast() const;
	void set_saturation(float p_saturation);
	float get_saturation() const;
	void set_tint(const Color &p_tint);
	Color get_tint() const;
	void set_lut(const Ref<Texture3D> &p_lut);
	Ref<Texture3D> get_lut() const;
};
