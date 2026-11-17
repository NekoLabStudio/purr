#pragma once

// LightPool3D: many light points (street lamps, lit shopfronts), and a few real lights moved to
// the best of them round the observer. The points are emissive or not drawn at all; the lights
// light what's near. Points rank by distance over the square root of their strength; a light keeps
// its point while it's still among the best, fades as it nears the rank of the first point left
// out, and eases to each new brightness, so none pops on or off as the observer moves.

#include "core/templates/hash_map.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/node_3d.h"

class LightPool3D : public Node3D {
	GDCLASS(LightPool3D, Node3D);

	struct Point {
		Vector3 position;
		float strength = 1.0;
		Color color = Color(1, 1, 1);
	};

	struct Slot {
		ObjectID light;
		int point = -1;
		float target = 0.0;
		float weight = 0.0; // Its fade towards the cut, from the last ranking.
	};

	HashMap<int, Point> points;
	HashMap<Vector2i, Vector<int>> grid; // Points by the grid squares they're in.
	int next_id = 0;

	int count = 16;
	float reach = 200.0;
	float height_weight = 1.0;
	float reach_above = 0.0;
	float brightness = 1.0;
	ObjectID observer_id;

	Vector<Slot> slots;
	ObjectID template_id;
	float template_energy = 1.0;
	float template_range = 10.0;
	Color template_color = Color(1, 1, 1);
	float refresh_in = 0.0;

	static constexpr float GRID_SQUARE = 64.0;
	static constexpr float REFRESH_EVERY = 0.25; // Seconds between rankings.
	static constexpr float FADE = 0.2; // The share of the cut over which a light fades before it.
	static constexpr float EASE = 6.0; // How quickly lights ease to a new brightness (per second).

	Vector2i _square(const Vector3 &p_position) const;
	Vector3 _eye() const;
	void _make_lights();
	void _free_lights();
	void _refresh();
	void _aim(Slot &p_slot, int p_point, float p_weight, bool p_keep_weight = false);
	void _ease(float p_delta);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	int add_point(const Vector3 &p_position, float p_strength = 1.0, const Color &p_color = Color(1, 1, 1));
	void remove_point(int p_id);
	void set_point_strength(int p_id, float p_strength);
	float get_point_strength(int p_id) const;
	void set_point_color(int p_id, const Color &p_color);
	Color get_point_color(int p_id) const;
	void clear_points();
	int get_point_count() const;
	int get_lit_point(int p_slot) const;

	void set_count(int p_count);
	int get_count() const;
	void set_reach(float p_reach);
	float get_reach() const;
	void set_height_weight(float p_weight);
	float get_height_weight() const;
	void set_reach_above(float p_reach);
	float get_reach_above() const;
	void set_brightness(float p_brightness);
	float get_brightness() const;
	void set_observer(Node3D *p_observer);
	Node3D *get_observer() const;

	PackedStringArray get_configuration_warnings() const override;
};
