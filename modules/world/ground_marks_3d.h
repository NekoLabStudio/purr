#pragma once

// GroundMarks3D: marks left on the ground where it goes while it's emitting: a strip, for tyre marks
// and ski tracks, or stamps, for footprints and hoof prints. Each mark lies on the surface below
// (found by a ray down), fades out over its lifetime, and stays put when the world shifts.

#include "scene/3d/node_3d.h"
#include "scene/resources/material.h"

class MeshInstance3D;
class ArrayMesh;

class GroundMarks3D : public Node3D {
	GDCLASS(GroundMarks3D, Node3D);

public:
	enum Mode {
		MODE_STRIP,
		MODE_STAMPS,
	};

private:
	struct Mark {
		Vector3 position;
		Vector3 normal;
		Vector3 forward;
		double born = 0.0;
		bool starts = false; // A strip begins again here, after a gap.
	};

	bool emitting = false;
	Mode mode = MODE_STRIP;
	float width = 0.25;
	float spacing = 0.25;
	float length = 0.3; // A stamp's, along the way it goes.
	float lifetime = 30.0;
	int max_marks = 512;
	float ground_distance = 1.0;
	uint32_t collision_mask = 1;
	Ref<Material> material;

	LocalVector<Mark> marks;
	bool gap = true; // The next strip mark starts a new strip.
	Vector3 last_position;
	double clock = 0.0;
	double drawn_at = -1.0;
	bool changed = false;
	MeshInstance3D *instance = nullptr;
	Ref<ArrayMesh> mesh;

	bool _ground(const Vector3 &p_from, Vector3 &r_position, Vector3 &r_normal) const;
	void _add(const Vector3 &p_position, const Vector3 &p_normal, const Vector3 &p_forward);
	void _draw();
	static Ref<StandardMaterial3D> default_material;
	static Ref<Material> _default_material();

protected:
	void _notification(int p_what);
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();

public:
	virtual void world_shifted(const Vector3 &p_by) override;

	void set_emitting(bool p_emitting);
	bool is_emitting() const;
	void set_mode(Mode p_mode);
	Mode get_mode() const;
	void set_width(float p_width);
	float get_width() const;
	void set_spacing(float p_spacing);
	float get_spacing() const;
	void set_length(float p_length);
	float get_length() const;
	void set_lifetime(float p_seconds);
	float get_lifetime() const;
	void set_max_marks(int p_marks);
	int get_max_marks() const;
	void set_ground_distance(float p_metres);
	float get_ground_distance() const;
	void set_collision_mask(uint32_t p_mask);
	uint32_t get_collision_mask() const;
	void set_material(const Ref<Material> &p_material);
	Ref<Material> get_material() const;

	int get_mark_count() const;
	void clear();

	static void cleanup();
};

VARIANT_ENUM_CAST(GroundMarks3D::Mode);
