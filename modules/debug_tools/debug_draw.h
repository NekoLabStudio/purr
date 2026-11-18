#pragma once

// DebugDraw: shapes and text drawn from anywhere for one frame or a few seconds, to see what code
// is doing. Lines in 3D (with a faint copy showing through what's in front of them), lines in 2D
// in the world's canvas, text at points in either, and named values listed in a corner of the
// screen. Everything drawn in a frame goes to the renderer as one mesh, so thousands of shapes
// cost little. Shapes drawn during a physics tick last until the next tick, so they don't flicker
// when ticks and frames don't line up.

#include "performance_overlay.h"

#include "core/math/transform_3d.h"
#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/rid.h"
#include "scene/resources/material.h"

class Viewport;

class DebugDraw : public Object {
	GDCLASS(DebugDraw, Object);

	static DebugDraw *singleton;

	struct Text3D {
		Vector3 position;
		String text;
		Color color;
	};

	struct Text2D {
		Vector2 position;
		String text;
		Color color;
	};

	struct Shapes {
		Vector<Vector3> lines; // Pairs of points.
		Vector<Color> line_colors; // One a point.
		Vector<Vector2> lines_2d;
		Vector<Color> line_colors_2d; // One a line.
		Vector<Text3D> texts;
		Vector<Text2D> texts_2d;

		bool is_empty() const { return lines.is_empty() && lines_2d.is_empty() && texts.is_empty() && texts_2d.is_empty(); }
		void clear();
		void append(const Shapes &p_other);
		void move(const Vector3 &p_by);
	};

	struct Timed {
		Shapes shapes;
		uint64_t until_usec = 0;
	};

	struct Value {
		String text;
		uint64_t until_usec = 0; // 0: until the end of the frame (or the next physics tick).
		bool from_physics = false;
	};

	Shapes frame_shapes; // Drawn during this frame's processing.
	Shapes tick_shapes; // Drawn during the last physics tick.
	Vector<Timed> timed;
	HashMap<String, Value> values;

	bool enabled = true;
	bool started = false;
	mutable Mutex mutex; // Drawing can come from any thread. Recursive, so calls within calls are fine.
	bool drawn_this_frame = false;
	bool drew_last_frame = false;

	RID mesh;
	RID instance;
	RID see_through_instance;
	RID scenario;
	Ref<StandardMaterial3D> material;
	Ref<StandardMaterial3D> see_through_material;
	RID world_item; // In the world's 2D canvas.
	RID world_canvas;
	RID overlay; // A canvas above everything else on screen.
	RID overlay_item;
	RID overlay_viewport;
	bool performance_overlay = false;
	PerformanceOverlay performance;

	void _start();
	bool _accepting() const { return enabled && started; }
	Shapes &_target(float p_duration);
	void _add_line(Shapes &p_shapes, const Vector3 &p_from, const Vector3 &p_to, const Color &p_color);
	void _add_line_2d(Shapes &p_shapes, const Vector2 &p_from, const Vector2 &p_to, const Color &p_color);
	void _add_circle(Shapes &p_shapes, const Vector3 &p_center, const Vector3 &p_axis_a, const Vector3 &p_axis_b, const Color &p_color);
	void _expire(uint64_t p_now);
	void _drop_values(bool p_from_physics);
	void _process_frame();
	void _physics_frame();
	void _pre_draw();
	void _draw_3d(Viewport *p_root, const Shapes &p_shapes);
	void _draw_2d(Viewport *p_root, const Shapes &p_shapes);
	void _ensure_overlay(Viewport *p_root);
	void _draw_overlay(Viewport *p_root, const Shapes &p_shapes);
	void _shifted(const Vector3 &p_by);
	void _free_resources();

protected:
	static void _bind_methods();

public:
	static DebugDraw *get_singleton() { return singleton; }

	void set_enabled(bool p_enabled);
	bool is_enabled() const;
	void set_performance_overlay(bool p_shown);
	bool is_performance_overlay() const;

	void line(const Vector3 &p_from, const Vector3 &p_to, const Color &p_color = Color(1, 1, 1), float p_duration = 0.0);
	void path(const PackedVector3Array &p_points, const Color &p_color = Color(1, 1, 1), float p_duration = 0.0, bool p_closed = false);
	void arrow(const Vector3 &p_from, const Vector3 &p_to, const Color &p_color = Color(1, 1, 1), float p_duration = 0.0);
	void point(const Vector3 &p_position, float p_size = 0.25, const Color &p_color = Color(1, 1, 1), float p_duration = 0.0);
	void box(const Transform3D &p_transform, const Color &p_color = Color(1, 1, 1), float p_duration = 0.0);
	void aabb(const AABB &p_aabb, const Color &p_color = Color(1, 1, 1), float p_duration = 0.0);
	void sphere(const Vector3 &p_center, float p_radius = 0.5, const Color &p_color = Color(1, 1, 1), float p_duration = 0.0);
	void axes(const Transform3D &p_transform, float p_size = 1.0, float p_duration = 0.0);
	void text(const Vector3 &p_position, const String &p_text, const Color &p_color = Color(1, 1, 1), float p_duration = 0.0);

	void line_2d(const Vector2 &p_from, const Vector2 &p_to, const Color &p_color = Color(1, 1, 1), float p_duration = 0.0);
	void path_2d(const PackedVector2Array &p_points, const Color &p_color = Color(1, 1, 1), float p_duration = 0.0, bool p_closed = false);
	void arrow_2d(const Vector2 &p_from, const Vector2 &p_to, const Color &p_color = Color(1, 1, 1), float p_duration = 0.0);
	void rect_2d(const Rect2 &p_rect, const Color &p_color = Color(1, 1, 1), float p_duration = 0.0);
	void circle_2d(const Vector2 &p_center, float p_radius, const Color &p_color = Color(1, 1, 1), float p_duration = 0.0);
	void text_2d(const Vector2 &p_position, const String &p_text, const Color &p_color = Color(1, 1, 1), float p_duration = 0.0);

	void show_value(const String &p_name, const Variant &p_value, float p_duration = 0.0);
	void clear();

	int get_line_count() const;

	DebugDraw();
	~DebugDraw();
};
