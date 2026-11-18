#pragma once

// VirtualCamera2D: a place the viewport's Camera2D can be. Each works out where it should be
// every frame (following a target, or framing a group with a zoom that fits them all); the real
// camera takes the position and zoom of the one with the highest priority, blending over when
// that changes.

#include "scene/2d/node_2d.h"
#include "scene/animation/tween.h"

class VirtualCamera2D : public Node2D {
	GDCLASS(VirtualCamera2D, Node2D);

public:
	enum FollowMode {
		FOLLOW_NONE,
		FOLLOW_TARGET,
		FOLLOW_GROUP,
	};

private:
	int priority = 0;
	FollowMode follow_mode = FOLLOW_NONE;
	NodePath follow_target;
	TypedArray<NodePath> follow_targets;
	Vector2 follow_offset;
	real_t follow_damping = 0.0;
	real_t group_margin = 64.0;
	Vector2 zoom = Vector2(1, 1);
	real_t blend_time = 1.0;
	Tween::TransitionType blend_transition = Tween::TRANS_CUBIC;
	Tween::EaseType blend_ease = Tween::EASE_IN_OUT;

	uint64_t ranked_at = 0;
	bool live = false;
	bool placed = false;
	Vector2 current_zoom = Vector2(1, 1);
	real_t shake_strength = 0.0;
	real_t shake_left = 0.0;
	real_t shake_duration = 0.0;
	real_t shake_time = 0.0;

	Node2D *_node(const NodePath &p_path) const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_priority(int p_priority);
	int get_priority() const;
	void set_follow_mode(FollowMode p_mode);
	FollowMode get_follow_mode() const;
	void set_follow_target(const NodePath &p_target);
	NodePath get_follow_target() const;
	void set_follow_targets(const TypedArray<NodePath> &p_targets);
	TypedArray<NodePath> get_follow_targets() const;
	void set_follow_offset(const Vector2 &p_offset);
	Vector2 get_follow_offset() const;
	void set_follow_damping(real_t p_seconds);
	real_t get_follow_damping() const;
	void set_group_margin(real_t p_pixels);
	real_t get_group_margin() const;
	void set_zoom(const Vector2 &p_zoom);
	Vector2 get_zoom() const;
	void set_blend_time(real_t p_seconds);
	real_t get_blend_time() const;
	void set_blend_transition(Tween::TransitionType p_transition);
	Tween::TransitionType get_blend_transition() const;
	void set_blend_ease(Tween::EaseType p_ease);
	Tween::EaseType get_blend_ease() const;

	bool is_live() const;
	void shake(real_t p_strength = 1.0, real_t p_duration = 0.5);
	void snap();

	// For the director.
	uint64_t get_ranked_at() const { return ranked_at; }
	void update_pose(real_t p_delta);
	Vector2 get_shaken_position() const;
	Vector2 get_current_zoom() const { return current_zoom; }
	void set_live(bool p_live);

	PackedStringArray get_configuration_warnings() const override;

	VirtualCamera2D();
};

VARIANT_ENUM_CAST(VirtualCamera2D::FollowMode);
