#include "virtual_camera_3d.h"

#include "camera_director.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/path_3d.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/physics_3d/physics_server_3d.h"

namespace {
constexpr real_t COLLISION_MARGIN = 0.2; // Kept between an orbiting camera and a wall.
constexpr real_t SHAKE_DEGREES = 3.0; // At a strength of 1.

// 0 to 1: how far to go towards where it should be this frame, for a damping in seconds.
real_t damping_weight(real_t p_damping, real_t p_delta) {
	return p_damping <= 0.0 ? 1.0 : 1.0 - Math::exp(-p_delta / p_damping);
}

// Smooth noise from -1 to 1, different for each seed.
real_t wobble(real_t p_time, real_t p_seed) {
	return Math::sin(p_time * 17.3 + p_seed) * 0.6 + Math::sin(p_time * 41.9 + p_seed * 2.1) * 0.4;
}
} // namespace

VirtualCamera3D::VirtualCamera3D() {
	// It's moved every frame, so there's nothing to interpolate.
	set_physics_interpolation_mode(PHYSICS_INTERPOLATION_MODE_OFF);
}

void VirtualCamera3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE:
			if (!Engine::get_singleton()->is_editor_hint()) {
				ranked_at = OS::get_singleton()->get_ticks_usec();
				placed = false;
				CameraDirector::add_camera(this);
			}
			break;
		case NOTIFICATION_EXIT_TREE:
			if (!Engine::get_singleton()->is_editor_hint()) {
				CameraDirector::remove_camera(this);
				set_live(false);
			}
			break;
	}
}

Node3D *VirtualCamera3D::_node(const NodePath &p_path) const {
	return p_path.is_empty() ? nullptr : Object::cast_to<Node3D>(get_node_or_null(p_path));
}

// Where a node is drawn this frame, between physics ticks when it moves in them.
Vector3 VirtualCamera3D::_position_of(Node3D *p_node) {
	return p_node->get_global_transform_interpolated().origin;
}

// Round the pivot at the orbit's angles, pulled in front of anything in between (at once, so it
// never shows the inside of a wall) and easing back out.
Vector3 VirtualCamera3D::_orbit_position(const Vector3 &p_pivot, real_t p_delta) {
	const Vector3 direction = Basis::from_euler(Vector3(orbit_pitch, orbit_yaw, 0)).xform(Vector3(0, 0, 1));
	real_t distance = orbit_distance;
	if (orbit_collision_mask != 0 && get_world_3d().is_valid()) {
		PhysicsDirectSpaceState3D *space = get_world_3d()->get_direct_space_state();
		if (space != nullptr) {
			PhysicsServer3DTypes::RayParameters ray;
			ray.from = p_pivot;
			ray.to = p_pivot + direction * (orbit_distance + COLLISION_MARGIN);
			ray.collision_mask = orbit_collision_mask;
			// The target's own body isn't in the way: the target, or the body it's on (a point on a
			// character, such as its head).
			for (Node *node = _node(follow_target); node != nullptr; node = node->get_parent()) {
				if (CollisionObject3D *body = Object::cast_to<CollisionObject3D>(node)) {
					ray.exclude.insert(body->get_rid());
					break;
				}
			}
			PhysicsServer3DTypes::RayResult hit;
			if (space->intersect_ray(ray, hit)) {
				distance = MAX(p_pivot.distance_to(hit.position) - COLLISION_MARGIN, 0.0);
			}
		}
	}
	if (!placed || distance < current_distance) {
		current_distance = distance;
	} else {
		current_distance = Math::lerp(current_distance, distance, damping_weight(follow_damping, p_delta));
	}
	return p_pivot + direction * current_distance;
}

void VirtualCamera3D::update_pose(real_t p_delta) {
	Node3D *target = _node(follow_target);
	const Transform3D current = get_global_transform();
	Vector3 position = current.origin;
	Vector3 look_point;
	bool looks = false;

	switch (follow_mode) {
		case FOLLOW_NONE:
			break;
		case FOLLOW_TARGET:
			if (target != nullptr) {
				position = _position_of(target) + follow_offset;
			}
			break;
		case FOLLOW_ORBIT:
			if (target != nullptr) {
				const Vector3 pivot = _position_of(target) + follow_offset;
				position = _orbit_position(pivot, p_delta);
				look_point = pivot;
				looks = true;
			}
			break;
		case FOLLOW_PATH: {
			Path3D *path = Object::cast_to<Path3D>(get_node_or_null(follow_path));
			if (target != nullptr && path != nullptr && path->get_curve().is_valid() && path->get_curve()->get_point_count() > 1) {
				const Ref<Curve3D> curve = path->get_curve();
				const Transform3D path_transform = path->get_global_transform();
				const Vector3 target_position = _position_of(target);
				const real_t along = curve->get_closest_offset(path_transform.affine_inverse().xform(target_position));
				position = path_transform.xform(curve->sample_baked(along, true));
				look_point = target_position;
				looks = true;
			}
		} break;
		case FOLLOW_GROUP: {
			AABB bounds;
			bool any = false;
			for (const NodePath &each : follow_targets) {
				Node3D *member = _node(each);
				if (member == nullptr) {
					continue;
				}
				if (!any) {
					bounds = AABB(_position_of(member), Vector3());
					any = true;
				} else {
					bounds.expand_to(_position_of(member));
				}
			}
			if (any) {
				// Back along the offset far enough for the group to fit in view, and no nearer
				// than the offset itself.
				const real_t radius = bounds.size.length() * 0.5;
				const real_t needed = radius / Math::tan(Math::deg_to_rad(fov) * 0.5) * 1.15;
				const Vector3 away = follow_offset.is_zero_approx() ? Vector3(0, 0, 1) : follow_offset.normalized();
				position = bounds.get_center() + away * MAX(follow_offset.length(), needed);
				look_point = bounds.get_center();
				looks = true;
			}
		} break;
	}

	if (follow_mode != FOLLOW_NONE && placed && follow_mode != FOLLOW_ORBIT) {
		position = current.origin.lerp(position, damping_weight(follow_damping, p_delta));
	}
	Node3D *look_target = _node(look_at_target);
	if (look_target != nullptr) {
		look_point = _position_of(look_target);
		looks = true;
	}
	Basis basis = current.basis.orthonormalized();
	if (looks && !look_point.is_equal_approx(position)) {
		const Vector3 forward = look_point - position;
		const Vector3 up = Math::abs(forward.normalized().dot(Vector3(0, 1, 0))) > 0.999 ? Vector3(0, 0, -1) : Vector3(0, 1, 0);
		const Basis wanted = Basis::looking_at(forward, up);
		basis = placed ? Basis(basis.get_rotation_quaternion().slerp(wanted.get_rotation_quaternion(), damping_weight(look_damping, p_delta))) : wanted;
	}
	placed = true;
	set_global_transform(Transform3D(basis, position));
	_update_focus(p_delta);
	if (fade_obstacles && live && target != nullptr) {
		fader.update(get_world_3d().ptr(), target, position, orbit_collision_mask, fade_amount, p_delta);
	} else {
		fader.clear();
	}

	if (shake_left > 0.0) {
		shake_left = MAX(shake_left - p_delta, 0.0);
		shake_time += p_delta;
	}
}

// How far away what's in focus is: what's in the middle of the view, or the target.
void VirtualCamera3D::_update_focus(real_t p_delta) {
	if (focus_mode == FOCUS_OFF) {
		focus_placed = false;
		return;
	}
	const Transform3D pose = get_global_transform();
	real_t wanted = focus_distance;
	if (focus_mode == FOCUS_TARGET) {
		Node3D *subject = _node(look_at_target);
		if (subject == nullptr) {
			subject = _node(follow_target);
		}
		if (subject != nullptr) {
			wanted = pose.origin.distance_to(_position_of(subject));
		}
	} else {
		constexpr real_t FURTHEST = 500.0;
		wanted = FURTHEST;
		PhysicsDirectSpaceState3D *space = is_inside_tree() ? get_world_3d()->get_direct_space_state() : nullptr;
		if (space != nullptr) {
			PhysicsServer3DTypes::RayParameters ray;
			ray.from = pose.origin;
			ray.to = pose.origin - pose.basis.get_column(Vector3::AXIS_Z).normalized() * FURTHEST;
			ray.collision_mask = focus_collision_mask;
			PhysicsServer3DTypes::RayResult hit;
			if (space->intersect_ray(ray, hit)) {
				wanted = pose.origin.distance_to(hit.position);
			}
		}
	}
	wanted = MAX(wanted, 0.1);
	focus_distance = focus_placed ? Math::lerp(focus_distance, wanted, damping_weight(focus_damping, p_delta)) : wanted;
	focus_placed = true;
}

real_t VirtualCamera3D::get_focus_distance() const {
	return focus_distance;
}

Transform3D VirtualCamera3D::get_shaken_transform() const {
	Transform3D pose = get_global_transform();
	if (shake_left <= 0.0 || shake_duration <= 0.0) {
		return pose;
	}
	const real_t fade = shake_left / shake_duration;
	const real_t angle = Math::deg_to_rad(SHAKE_DEGREES) * shake_strength * fade * fade;
	const Vector3 wobbled(wobble(shake_time, 0.0), wobble(shake_time, 3.7), wobble(shake_time, 7.1) * 0.5);
	pose.basis = pose.basis * Basis::from_euler(wobbled * angle);
	return pose;
}

void VirtualCamera3D::set_live(bool p_live) {
	if (live == p_live) {
		return;
	}
	live = p_live;
	if (!live) {
		fader.clear();
	}
	emit_signal(SNAME("live_changed"), live);
}

bool VirtualCamera3D::is_live() const {
	return live;
}

void VirtualCamera3D::shake(real_t p_strength, real_t p_duration) {
	// A stronger shake takes over a fading one; a weaker one doesn't cut it short.
	const real_t fade = shake_duration > 0.0 ? shake_left / shake_duration : 0.0;
	if (p_strength >= shake_strength * fade * fade) {
		shake_strength = p_strength;
		shake_duration = MAX(p_duration, 0.01);
		shake_left = shake_duration;
	}
}

void VirtualCamera3D::snap() {
	placed = false;
}

PackedStringArray VirtualCamera3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (follow_mode != FOLLOW_NONE && follow_mode != FOLLOW_GROUP && follow_target.is_empty()) {
		warnings.push_back(RTR("Set a follow target for this follow mode."));
	}
	if (follow_mode == FOLLOW_PATH && follow_path.is_empty()) {
		warnings.push_back(RTR("Set the Path3D to follow along."));
	}
	if (follow_mode == FOLLOW_GROUP && follow_targets.is_empty()) {
		warnings.push_back(RTR("Add the nodes to frame to follow targets."));
	}
	return warnings;
}

void VirtualCamera3D::set_priority(int p_priority) {
	priority = p_priority;
	ranked_at = OS::get_singleton()->get_ticks_usec();
}

int VirtualCamera3D::get_priority() const {
	return priority;
}

void VirtualCamera3D::set_follow_mode(FollowMode p_mode) {
	follow_mode = p_mode;
	placed = false;
	update_configuration_warnings();
	notify_property_list_changed();
}

VirtualCamera3D::FollowMode VirtualCamera3D::get_follow_mode() const {
	return follow_mode;
}

void VirtualCamera3D::set_follow_target(const NodePath &p_target) {
	follow_target = p_target;
	update_configuration_warnings();
}

NodePath VirtualCamera3D::get_follow_target() const {
	return follow_target;
}

void VirtualCamera3D::set_follow_targets(const TypedArray<NodePath> &p_targets) {
	follow_targets = p_targets;
	update_configuration_warnings();
}

TypedArray<NodePath> VirtualCamera3D::get_follow_targets() const {
	return follow_targets;
}

void VirtualCamera3D::set_follow_offset(const Vector3 &p_offset) {
	follow_offset = p_offset;
}

Vector3 VirtualCamera3D::get_follow_offset() const {
	return follow_offset;
}

void VirtualCamera3D::set_follow_damping(real_t p_seconds) {
	follow_damping = MAX(p_seconds, 0.0);
}

real_t VirtualCamera3D::get_follow_damping() const {
	return follow_damping;
}

void VirtualCamera3D::set_orbit_distance(real_t p_distance) {
	orbit_distance = MAX(p_distance, 0.0);
}

real_t VirtualCamera3D::get_orbit_distance() const {
	return orbit_distance;
}

void VirtualCamera3D::set_orbit_pitch(real_t p_radians) {
	orbit_pitch = CLAMP(p_radians, -Math::PI * 0.49, Math::PI * 0.49);
}

real_t VirtualCamera3D::get_orbit_pitch() const {
	return orbit_pitch;
}

void VirtualCamera3D::set_orbit_yaw(real_t p_radians) {
	orbit_yaw = Math::wrapf(p_radians, real_t(-Math::PI), real_t(Math::PI));
}

real_t VirtualCamera3D::get_orbit_yaw() const {
	return orbit_yaw;
}

void VirtualCamera3D::set_orbit_collision_mask(uint32_t p_mask) {
	orbit_collision_mask = p_mask;
}

uint32_t VirtualCamera3D::get_orbit_collision_mask() const {
	return orbit_collision_mask;
}

void VirtualCamera3D::set_follow_path(const NodePath &p_path) {
	follow_path = p_path;
	update_configuration_warnings();
}

NodePath VirtualCamera3D::get_follow_path() const {
	return follow_path;
}

void VirtualCamera3D::set_look_at_target(const NodePath &p_target) {
	look_at_target = p_target;
}

NodePath VirtualCamera3D::get_look_at_target() const {
	return look_at_target;
}

void VirtualCamera3D::set_look_damping(real_t p_seconds) {
	look_damping = MAX(p_seconds, 0.0);
}

real_t VirtualCamera3D::get_look_damping() const {
	return look_damping;
}

void VirtualCamera3D::set_fov(real_t p_degrees) {
	fov = CLAMP(p_degrees, 1.0, 179.0);
}

real_t VirtualCamera3D::get_fov() const {
	return fov;
}

void VirtualCamera3D::set_focus_mode(FocusMode p_mode) {
	focus_mode = p_mode;
	focus_placed = false;
}

VirtualCamera3D::FocusMode VirtualCamera3D::get_focus_mode() const {
	return focus_mode;
}

void VirtualCamera3D::set_focus_blur(real_t p_blur) {
	focus_blur = CLAMP(p_blur, 0.0, 1.0);
}

real_t VirtualCamera3D::get_focus_blur() const {
	return focus_blur;
}

void VirtualCamera3D::set_focus_damping(real_t p_seconds) {
	focus_damping = MAX(p_seconds, 0.0);
}

real_t VirtualCamera3D::get_focus_damping() const {
	return focus_damping;
}

void VirtualCamera3D::set_focus_collision_mask(uint32_t p_mask) {
	focus_collision_mask = p_mask;
}

uint32_t VirtualCamera3D::get_focus_collision_mask() const {
	return focus_collision_mask;
}

void VirtualCamera3D::set_fade_obstacles(bool p_fade) {
	fade_obstacles = p_fade;
	if (!fade_obstacles) {
		fader.clear();
	}
}

bool VirtualCamera3D::is_fading_obstacles() const {
	return fade_obstacles;
}

void VirtualCamera3D::set_fade_amount(real_t p_amount) {
	fade_amount = CLAMP(p_amount, 0.0, 1.0);
}

real_t VirtualCamera3D::get_fade_amount() const {
	return fade_amount;
}

void VirtualCamera3D::set_blend_time(real_t p_seconds) {
	blend_time = MAX(p_seconds, 0.0);
}

real_t VirtualCamera3D::get_blend_time() const {
	return blend_time;
}

void VirtualCamera3D::set_blend_transition(Tween::TransitionType p_transition) {
	blend_transition = p_transition;
}

Tween::TransitionType VirtualCamera3D::get_blend_transition() const {
	return blend_transition;
}

void VirtualCamera3D::set_blend_ease(Tween::EaseType p_ease) {
	blend_ease = p_ease;
}

Tween::EaseType VirtualCamera3D::get_blend_ease() const {
	return blend_ease;
}

void VirtualCamera3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_priority", "priority"), &VirtualCamera3D::set_priority);
	ClassDB::bind_method(D_METHOD("get_priority"), &VirtualCamera3D::get_priority);
	ClassDB::bind_method(D_METHOD("set_follow_mode", "mode"), &VirtualCamera3D::set_follow_mode);
	ClassDB::bind_method(D_METHOD("get_follow_mode"), &VirtualCamera3D::get_follow_mode);
	ClassDB::bind_method(D_METHOD("set_follow_target", "target"), &VirtualCamera3D::set_follow_target);
	ClassDB::bind_method(D_METHOD("get_follow_target"), &VirtualCamera3D::get_follow_target);
	ClassDB::bind_method(D_METHOD("set_follow_targets", "targets"), &VirtualCamera3D::set_follow_targets);
	ClassDB::bind_method(D_METHOD("get_follow_targets"), &VirtualCamera3D::get_follow_targets);
	ClassDB::bind_method(D_METHOD("set_follow_offset", "offset"), &VirtualCamera3D::set_follow_offset);
	ClassDB::bind_method(D_METHOD("get_follow_offset"), &VirtualCamera3D::get_follow_offset);
	ClassDB::bind_method(D_METHOD("set_follow_damping", "seconds"), &VirtualCamera3D::set_follow_damping);
	ClassDB::bind_method(D_METHOD("get_follow_damping"), &VirtualCamera3D::get_follow_damping);
	ClassDB::bind_method(D_METHOD("set_orbit_distance", "distance"), &VirtualCamera3D::set_orbit_distance);
	ClassDB::bind_method(D_METHOD("get_orbit_distance"), &VirtualCamera3D::get_orbit_distance);
	ClassDB::bind_method(D_METHOD("set_orbit_pitch", "radians"), &VirtualCamera3D::set_orbit_pitch);
	ClassDB::bind_method(D_METHOD("get_orbit_pitch"), &VirtualCamera3D::get_orbit_pitch);
	ClassDB::bind_method(D_METHOD("set_orbit_yaw", "radians"), &VirtualCamera3D::set_orbit_yaw);
	ClassDB::bind_method(D_METHOD("get_orbit_yaw"), &VirtualCamera3D::get_orbit_yaw);
	ClassDB::bind_method(D_METHOD("set_orbit_collision_mask", "mask"), &VirtualCamera3D::set_orbit_collision_mask);
	ClassDB::bind_method(D_METHOD("get_orbit_collision_mask"), &VirtualCamera3D::get_orbit_collision_mask);
	ClassDB::bind_method(D_METHOD("set_follow_path", "path"), &VirtualCamera3D::set_follow_path);
	ClassDB::bind_method(D_METHOD("get_follow_path"), &VirtualCamera3D::get_follow_path);
	ClassDB::bind_method(D_METHOD("set_look_at_target", "target"), &VirtualCamera3D::set_look_at_target);
	ClassDB::bind_method(D_METHOD("get_look_at_target"), &VirtualCamera3D::get_look_at_target);
	ClassDB::bind_method(D_METHOD("set_look_damping", "seconds"), &VirtualCamera3D::set_look_damping);
	ClassDB::bind_method(D_METHOD("get_look_damping"), &VirtualCamera3D::get_look_damping);
	ClassDB::bind_method(D_METHOD("set_fov", "degrees"), &VirtualCamera3D::set_fov);
	ClassDB::bind_method(D_METHOD("get_fov"), &VirtualCamera3D::get_fov);
	ClassDB::bind_method(D_METHOD("set_focus_mode", "mode"), &VirtualCamera3D::set_focus_mode);
	ClassDB::bind_method(D_METHOD("get_focus_mode"), &VirtualCamera3D::get_focus_mode);
	ClassDB::bind_method(D_METHOD("set_focus_blur", "blur"), &VirtualCamera3D::set_focus_blur);
	ClassDB::bind_method(D_METHOD("get_focus_blur"), &VirtualCamera3D::get_focus_blur);
	ClassDB::bind_method(D_METHOD("set_focus_damping", "seconds"), &VirtualCamera3D::set_focus_damping);
	ClassDB::bind_method(D_METHOD("get_focus_damping"), &VirtualCamera3D::get_focus_damping);
	ClassDB::bind_method(D_METHOD("set_focus_collision_mask", "mask"), &VirtualCamera3D::set_focus_collision_mask);
	ClassDB::bind_method(D_METHOD("get_focus_collision_mask"), &VirtualCamera3D::get_focus_collision_mask);
	ClassDB::bind_method(D_METHOD("set_fade_obstacles", "fade"), &VirtualCamera3D::set_fade_obstacles);
	ClassDB::bind_method(D_METHOD("is_fading_obstacles"), &VirtualCamera3D::is_fading_obstacles);
	ClassDB::bind_method(D_METHOD("set_fade_amount", "amount"), &VirtualCamera3D::set_fade_amount);
	ClassDB::bind_method(D_METHOD("get_fade_amount"), &VirtualCamera3D::get_fade_amount);
	ClassDB::bind_method(D_METHOD("get_focus_distance"), &VirtualCamera3D::get_focus_distance);
	ClassDB::bind_method(D_METHOD("set_blend_time", "seconds"), &VirtualCamera3D::set_blend_time);
	ClassDB::bind_method(D_METHOD("get_blend_time"), &VirtualCamera3D::get_blend_time);
	ClassDB::bind_method(D_METHOD("set_blend_transition", "transition"), &VirtualCamera3D::set_blend_transition);
	ClassDB::bind_method(D_METHOD("get_blend_transition"), &VirtualCamera3D::get_blend_transition);
	ClassDB::bind_method(D_METHOD("set_blend_ease", "ease"), &VirtualCamera3D::set_blend_ease);
	ClassDB::bind_method(D_METHOD("get_blend_ease"), &VirtualCamera3D::get_blend_ease);
	ClassDB::bind_method(D_METHOD("is_live"), &VirtualCamera3D::is_live);
	ClassDB::bind_method(D_METHOD("shake", "strength", "duration"), &VirtualCamera3D::shake, DEFVAL(1.0), DEFVAL(0.5));
	ClassDB::bind_method(D_METHOD("snap"), &VirtualCamera3D::snap);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "priority"), "set_priority", "get_priority");
	ADD_GROUP("Follow", "follow_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "follow_mode", PROPERTY_HINT_ENUM, "None,Target,Orbit,Path,Group"), "set_follow_mode", "get_follow_mode");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "follow_target", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node3D"), "set_follow_target", "get_follow_target");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "follow_targets", PROPERTY_HINT_ARRAY_TYPE, vformat("%s/%s:%s", Variant::NODE_PATH, PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node3D")), "set_follow_targets", "get_follow_targets");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "follow_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Path3D"), "set_follow_path", "get_follow_path");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "follow_offset", PROPERTY_HINT_NONE, "suffix:m"), "set_follow_offset", "get_follow_offset");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "follow_damping", PROPERTY_HINT_RANGE, "0,5,0.01,or_greater,suffix:s"), "set_follow_damping", "get_follow_damping");
	ADD_GROUP("Orbit", "orbit_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "orbit_distance", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater,suffix:m"), "set_orbit_distance", "get_orbit_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "orbit_pitch", PROPERTY_HINT_RANGE, "-88,88,0.1,radians_as_degrees"), "set_orbit_pitch", "get_orbit_pitch");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "orbit_yaw", PROPERTY_HINT_RANGE, "-180,180,0.1,radians_as_degrees"), "set_orbit_yaw", "get_orbit_yaw");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "orbit_collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_orbit_collision_mask", "get_orbit_collision_mask");
	ADD_GROUP("Look", "look_");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "look_at_target", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node3D"), "set_look_at_target", "get_look_at_target");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "look_damping", PROPERTY_HINT_RANGE, "0,5,0.01,or_greater,suffix:s"), "set_look_damping", "get_look_damping");
	ADD_GROUP("Lens", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fov", PROPERTY_HINT_RANGE, "1,179,0.1,degrees"), "set_fov", "get_fov");
	ADD_GROUP("Focus", "focus_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "focus_mode", PROPERTY_HINT_ENUM, "Off,Center,Target"), "set_focus_mode", "get_focus_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "focus_blur", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_focus_blur", "get_focus_blur");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "focus_damping", PROPERTY_HINT_RANGE, "0,5,0.01,or_greater,suffix:s"), "set_focus_damping", "get_focus_damping");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "focus_collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_focus_collision_mask", "get_focus_collision_mask");
	ADD_GROUP("Obstacles", "fade_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fade_obstacles"), "set_fade_obstacles", "is_fading_obstacles");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_amount", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_fade_amount", "get_fade_amount");
	ADD_GROUP("Blend", "blend_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blend_time", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s"), "set_blend_time", "get_blend_time");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "blend_transition", PROPERTY_HINT_ENUM, "Linear,Sine,Quint,Quart,Quad,Expo,Elastic,Cubic,Circ,Bounce,Back,Spring"), "set_blend_transition", "get_blend_transition");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "blend_ease", PROPERTY_HINT_ENUM, "In,Out,In Out,Out In"), "set_blend_ease", "get_blend_ease");

	ADD_SIGNAL(MethodInfo("live_changed", PropertyInfo(Variant::BOOL, "live")));

	BIND_ENUM_CONSTANT(FOLLOW_NONE);
	BIND_ENUM_CONSTANT(FOLLOW_TARGET);
	BIND_ENUM_CONSTANT(FOLLOW_ORBIT);
	BIND_ENUM_CONSTANT(FOLLOW_PATH);
	BIND_ENUM_CONSTANT(FOLLOW_GROUP);
	BIND_ENUM_CONSTANT(FOCUS_OFF);
	BIND_ENUM_CONSTANT(FOCUS_CENTER);
	BIND_ENUM_CONSTANT(FOCUS_TARGET);
}
