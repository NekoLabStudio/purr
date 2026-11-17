#pragma once

// ReflectionProbePool: reflection probes at the nearest of many candidate sites round the
// observer (each junction of a city's streets, say), so reflections everywhere show the place
// round them without probes all over the world. A capture draws the scene round the probe, so
// new probes are placed one a frame, and recapture() redoes them one a frame as the light changes.

#include "core/templates/hash_map.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/reflection_probe.h"

class ReflectionProbePool : public Node3D {
	GDCLASS(ReflectionProbePool, Node3D);

	struct Site {
		Transform3D transform;
		Vector3 size;
		Vector3 origin_offset;
		ObjectID probe;
	};

	HashMap<int, Site> sites;
	HashMap<Vector2i, Vector<int>> grid;
	int next_id = 0;

	int count = 8;
	float reach = 150.0;
	float height_reach = 0.0;
	ObjectID observer_id;
	ObjectID template_id;

	Vector<int> waiting; // Sites wanted without a probe yet, nearest first.
	Vector<int> stale; // Sites whose probe is to be captured again.
	float look_in = 0.0;

	static constexpr float GRID_SQUARE = 64.0;
	static constexpr float LOOK_EVERY = 0.5;

	Vector2i _square(const Vector3 &p_position) const;
	Vector3 _eye() const;
	ReflectionProbe *_template() const;
	void _look();
	void _place(int p_id);
	void _drop(int p_id);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	int add_site(const Transform3D &p_transform, const Vector3 &p_size, const Vector3 &p_origin_offset = Vector3());
	void remove_site(int p_id);
	void clear_sites();
	int get_site_count() const;
	PackedInt32Array get_placed_sites() const;
	void recapture();

	void set_count(int p_count);
	int get_count() const;
	void set_reach(float p_reach);
	float get_reach() const;
	void set_height_reach(float p_reach);
	float get_height_reach() const;
	void set_observer(Node3D *p_observer);
	Node3D *get_observer() const;

	PackedStringArray get_configuration_warnings() const override;
};
