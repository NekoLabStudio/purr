#pragma once

// VirtualCamera3D: a place the viewport's Camera3D can be. Each virtual camera works out where it
// should be every frame (following a target, orbiting it, sliding along a path, framing a
// group) and what it looks at; the real camera takes the pose of the one with the highest
// priority, blending over when that changes. Put several in a scene and switch between them by
// priority.

#include "scene/3d/node_3d.h"
#include "scene/animation/tween.h"

class VirtualCamera3D : public Node3D {
	GDCLASS(VirtualCamera3D, Node3D);

public:
	enum FollowMode {
		FOLLOW_NONE,
		FOLLOW_TARGET,
		FOLLOW_ORBIT,
		FOLLOW_PATH,
		FOLLOW_GROUP,
	};

private:
	int priority = 0;
	FollowMode follow_mode = FOLLOW_NONE;
	NodePath follow_target;
	TypedArray<NodePath> follow_targets;
	Vector3 follow_offset = Vector3(0, 2, 5);
	real_t follow_damping = 0.0;
	real_t orbit_distance = 4.0;
	real_t orbit_pitch = Math::deg_to_rad(-15.0);
	real_t orbit_yaw = 0.0;
	uint32_t orbit_collision_mask = 1;
	NodePath follow_path;
	NodePath look_at_target;
	real_t look_damping = 0.0;
	real_t fov = 75.0;
	real_t blend_time = 1.0;
	Tween::TransitionType blend_transition = Tween::TRANS_CUBIC;
	Tween::EaseType blend_ease = Tween::EASE_IN_OUT;

	uint64_t ranked_at = 0; // When it entered the tree or its priority changed: the latest wins a tie.
	bool live = false;
	bool placed = false; // Has a pose from following yet, so damping has somewhere to start.
	real_t current_distance = 0.0;
	real_t shake_strength = 0.0;
	real_t shake_left = 0.0;
	real_t shake_duration = 0.0;
	real_t shake_time = 0.0;

	Node3D *_node(const NodePath &p_path) const;
	static Vector3 _position_of(Node3D *p_node);
	Vector3 _orbit_position(const Vector3 &p_pivot, real_t p_delta);

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
	void set_follow_offset(const Vector3 &p_offset);
	Vector3 get_follow_offset() const;
	void set_follow_damping(real_t p_seconds);
	real_t get_follow_damping() const;
	void set_orbit_distance(real_t p_distance);
	real_t get_orbit_distance() const;
	void set_orbit_pitch(real_t p_radians);
	real_t get_orbit_pitch() const;
	void set_orbit_yaw(real_t p_radians);
	real_t get_orbit_yaw() const;
	void set_orbit_collision_mask(uint32_t p_mask);
	uint32_t get_orbit_collision_mask() const;
	void set_follow_path(const NodePath &p_path);
	NodePath get_follow_path() const;
	void set_look_at_target(const NodePath &p_target);
	NodePath get_look_at_target() const;
	void set_look_damping(real_t p_seconds);
	real_t get_look_damping() const;
	void set_fov(real_t p_degrees);
	real_t get_fov() const;
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
	Transform3D get_shaken_transform() const;
	void set_live(bool p_live);

	PackedStringArray get_configuration_warnings() const override;

	VirtualCamera3D();
};

VARIANT_ENUM_CAST(VirtualCamera3D::FollowMode);
