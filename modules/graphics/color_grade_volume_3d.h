#pragma once

// ColorGradeVolume3D: a ColorGrade for a place, a time of day or a weather. Inside its box (or
// everywhere, if it's global) its grade is blended into the camera's environment, fading in over
// blend_distance as the camera comes in; when it applies can follow TimeOfDay and Weather3D, so a
// night, a dusk or a storm can have a look of its own. Volumes of higher priority go over lower.

#include "scene/3d/node_3d.h"
#include "scene/resources/environment.h"
#include "scene/resources/texture.h"

#include "color_grade.h"

class ColorGradeVolume3D : public Node3D {
	GDCLASS(ColorGradeVolume3D, Node3D);

public:
	enum When {
		WHEN_ALWAYS,
		WHEN_DAY,
		WHEN_NIGHT,
		WHEN_DAWN_AND_DUSK,
		WHEN_OVERCAST,
		WHEN_RAINING,
		WHEN_SNOWING,
	};

private:
	Ref<ColorGrade> grade;
	bool global = false;
	Vector3 size = Vector3(10, 10, 10);
	float blend_distance = 2.0;
	int priority = 0;
	float weight = 1.0;
	When when = WHEN_ALWAYS;

	static LocalVector<ColorGradeVolume3D *> volumes;

	static void _apply(Viewport *p_viewport);
	static void _restore();
	static void _capture(const Ref<Environment> &p_environment);

protected:
	void _notification(int p_what);
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();

public:
	void set_grade(const Ref<ColorGrade> &p_grade);
	Ref<ColorGrade> get_grade() const;
	void set_global(bool p_global);
	bool is_global() const;
	void set_size(const Vector3 &p_size);
	Vector3 get_size() const;
	void set_blend_distance(float p_metres);
	float get_blend_distance() const;
	void set_priority(int p_priority);
	int get_priority() const;
	void set_weight(float p_weight);
	float get_weight() const;
	void set_when(When p_when);
	When get_when() const;

	// How much of its grade applies with the camera at a place, now: 0 to 1.
	float get_influence(const Vector3 &p_camera) const;

	static void cleanup();
};

VARIANT_ENUM_CAST(ColorGradeVolume3D::When);
