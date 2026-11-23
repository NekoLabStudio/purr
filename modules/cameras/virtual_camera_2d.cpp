#include "virtual_camera_2d.h"

#include "camera_director.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/main/viewport.h"

namespace {
constexpr real_t SHAKE_PIXELS = 12.0; // At a strength of 1 and a zoom of 1.

real_t damping_weight(real_t p_damping, real_t p_delta) {
	return p_damping <= 0.0 ? 1.0 : 1.0 - Math::exp(-p_delta / p_damping);
}

real_t wobble(real_t p_time, real_t p_seed) {
	return Math::sin(p_time * 17.3 + p_seed) * 0.6 + Math::sin(p_time * 41.9 + p_seed * 2.1) * 0.4;
}
} // namespace

VirtualCamera2D::VirtualCamera2D() {
	// It's moved every frame, so there's nothing to interpolate.
	set_physics_interpolation_mode(PHYSICS_INTERPOLATION_MODE_OFF);
}

void VirtualCamera2D::_notification(int p_what) {
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

Node2D *VirtualCamera2D::_node(const NodePath &p_path) const {
	return p_path.is_empty() ? nullptr : Object::cast_to<Node2D>(get_node_or_null(p_path));
}

void VirtualCamera2D::update_pose(real_t p_delta) {
	Vector2 position = get_global_position();
	Vector2 wanted_zoom = zoom;
	switch (follow_mode) {
		case FOLLOW_NONE:
			break;
		case FOLLOW_TARGET: {
			Node2D *target = _node(follow_target);
			if (target != nullptr) {
				position = target->get_global_position() + follow_offset;
			}
		} break;
		case FOLLOW_GROUP: {
			Rect2 bounds;
			bool any = false;
			for (const NodePath &each : follow_targets) {
				Node2D *member = _node(each);
				if (member == nullptr) {
					continue;
				}
				if (!any) {
					bounds = Rect2(member->get_global_position(), Vector2());
					any = true;
				} else {
					bounds.expand_to(member->get_global_position());
				}
			}
			if (any) {
				position = bounds.get_center() + follow_offset;
				// Zoomed out enough for the group to fit inside the margin (pixels on screen, whatever
				// the zoom), and never in past zoom.
				const Vector2 view = get_viewport_rect().size - Vector2(group_margin, group_margin) * 2.0;
				const real_t fit = MIN(MAX(view.x, 1.0) / MAX(bounds.size.x, 1.0), MAX(view.y, 1.0) / MAX(bounds.size.y, 1.0));
				wanted_zoom = Vector2(MIN(fit, zoom.x), MIN(fit, zoom.y));
			}
		} break;
	}
	const real_t weight = placed ? damping_weight(follow_damping, p_delta) : 1.0;
	if (follow_mode != FOLLOW_NONE) {
		set_global_position(get_global_position().lerp(position, weight));
	}
	current_zoom = placed ? current_zoom.lerp(wanted_zoom, weight) : wanted_zoom;
	placed = true;

	if (shake_left > 0.0) {
		shake_left = MAX(shake_left - p_delta, 0.0);
		shake_time += p_delta;
	}
}

// Shaking moves the view by a few pixels on screen, whatever the zoom.
Vector2 VirtualCamera2D::get_shaken_position() const {
	const Vector2 position = get_global_position();
	if (shake_left <= 0.0 || shake_duration <= 0.0) {
		return position;
	}
	const real_t fade = shake_left / shake_duration;
	const real_t pixels = SHAKE_PIXELS * shake_strength * fade * fade;
	return position + Vector2(wobble(shake_time, 0.0), wobble(shake_time, 3.7)) * pixels / MAX(current_zoom.x, 0.01);
}

void VirtualCamera2D::set_live(bool p_live) {
	if (live == p_live) {
		return;
	}
	live = p_live;
	emit_signal(SNAME("live_changed"), live);
}

bool VirtualCamera2D::is_live() const {
	return live;
}

void VirtualCamera2D::shake(real_t p_strength, real_t p_duration) {
	const real_t fade = shake_duration > 0.0 ? shake_left / shake_duration : 0.0;
	if (p_strength >= shake_strength * fade * fade) {
		shake_strength = p_strength;
		shake_duration = MAX(p_duration, 0.01);
		shake_left = shake_duration;
	}
}

void VirtualCamera2D::snap() {
	placed = false;
}

PackedStringArray VirtualCamera2D::get_configuration_warnings() const {
	PackedStringArray warnings = Node2D::get_configuration_warnings();
	if (follow_mode == FOLLOW_TARGET && follow_target.is_empty()) {
		warnings.push_back(RTR("Set a follow target for this follow mode."));
	}
	if (follow_mode == FOLLOW_GROUP && follow_targets.is_empty()) {
		warnings.push_back(RTR("Add the nodes to frame to follow targets."));
	}
	return warnings;
}

void VirtualCamera2D::set_priority(int p_priority) {
	priority = p_priority;
	ranked_at = OS::get_singleton()->get_ticks_usec();
}

int VirtualCamera2D::get_priority() const {
	return priority;
}

void VirtualCamera2D::set_follow_mode(FollowMode p_mode) {
	follow_mode = p_mode;
	placed = false;
	update_configuration_warnings();
}

VirtualCamera2D::FollowMode VirtualCamera2D::get_follow_mode() const {
	return follow_mode;
}

void VirtualCamera2D::set_follow_target(const NodePath &p_target) {
	follow_target = p_target;
	update_configuration_warnings();
}

NodePath VirtualCamera2D::get_follow_target() const {
	return follow_target;
}

void VirtualCamera2D::set_follow_targets(const TypedArray<NodePath> &p_targets) {
	follow_targets = p_targets;
	update_configuration_warnings();
}

TypedArray<NodePath> VirtualCamera2D::get_follow_targets() const {
	return follow_targets;
}

void VirtualCamera2D::set_follow_offset(const Vector2 &p_offset) {
	follow_offset = p_offset;
}

Vector2 VirtualCamera2D::get_follow_offset() const {
	return follow_offset;
}

void VirtualCamera2D::set_follow_damping(real_t p_seconds) {
	follow_damping = MAX(p_seconds, 0.0);
}

real_t VirtualCamera2D::get_follow_damping() const {
	return follow_damping;
}

void VirtualCamera2D::set_group_margin(real_t p_pixels) {
	group_margin = MAX(p_pixels, 0.0);
}

real_t VirtualCamera2D::get_group_margin() const {
	return group_margin;
}

void VirtualCamera2D::set_zoom(const Vector2 &p_zoom) {
	zoom = Vector2(MAX(p_zoom.x, 0.01), MAX(p_zoom.y, 0.01));
}

Vector2 VirtualCamera2D::get_zoom() const {
	return zoom;
}

void VirtualCamera2D::set_blend_time(real_t p_seconds) {
	blend_time = MAX(p_seconds, 0.0);
}

real_t VirtualCamera2D::get_blend_time() const {
	return blend_time;
}

void VirtualCamera2D::set_blend_transition(Tween::TransitionType p_transition) {
	blend_transition = p_transition;
}

Tween::TransitionType VirtualCamera2D::get_blend_transition() const {
	return blend_transition;
}

void VirtualCamera2D::set_blend_ease(Tween::EaseType p_ease) {
	blend_ease = p_ease;
}

Tween::EaseType VirtualCamera2D::get_blend_ease() const {
	return blend_ease;
}

void VirtualCamera2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_priority", "priority"), &VirtualCamera2D::set_priority);
	ClassDB::bind_method(D_METHOD("get_priority"), &VirtualCamera2D::get_priority);
	ClassDB::bind_method(D_METHOD("set_follow_mode", "mode"), &VirtualCamera2D::set_follow_mode);
	ClassDB::bind_method(D_METHOD("get_follow_mode"), &VirtualCamera2D::get_follow_mode);
	ClassDB::bind_method(D_METHOD("set_follow_target", "target"), &VirtualCamera2D::set_follow_target);
	ClassDB::bind_method(D_METHOD("get_follow_target"), &VirtualCamera2D::get_follow_target);
	ClassDB::bind_method(D_METHOD("set_follow_targets", "targets"), &VirtualCamera2D::set_follow_targets);
	ClassDB::bind_method(D_METHOD("get_follow_targets"), &VirtualCamera2D::get_follow_targets);
	ClassDB::bind_method(D_METHOD("set_follow_offset", "offset"), &VirtualCamera2D::set_follow_offset);
	ClassDB::bind_method(D_METHOD("get_follow_offset"), &VirtualCamera2D::get_follow_offset);
	ClassDB::bind_method(D_METHOD("set_follow_damping", "seconds"), &VirtualCamera2D::set_follow_damping);
	ClassDB::bind_method(D_METHOD("get_follow_damping"), &VirtualCamera2D::get_follow_damping);
	ClassDB::bind_method(D_METHOD("set_group_margin", "pixels"), &VirtualCamera2D::set_group_margin);
	ClassDB::bind_method(D_METHOD("get_group_margin"), &VirtualCamera2D::get_group_margin);
	ClassDB::bind_method(D_METHOD("set_zoom", "zoom"), &VirtualCamera2D::set_zoom);
	ClassDB::bind_method(D_METHOD("get_zoom"), &VirtualCamera2D::get_zoom);
	ClassDB::bind_method(D_METHOD("set_blend_time", "seconds"), &VirtualCamera2D::set_blend_time);
	ClassDB::bind_method(D_METHOD("get_blend_time"), &VirtualCamera2D::get_blend_time);
	ClassDB::bind_method(D_METHOD("set_blend_transition", "transition"), &VirtualCamera2D::set_blend_transition);
	ClassDB::bind_method(D_METHOD("get_blend_transition"), &VirtualCamera2D::get_blend_transition);
	ClassDB::bind_method(D_METHOD("set_blend_ease", "ease"), &VirtualCamera2D::set_blend_ease);
	ClassDB::bind_method(D_METHOD("get_blend_ease"), &VirtualCamera2D::get_blend_ease);
	ClassDB::bind_method(D_METHOD("is_live"), &VirtualCamera2D::is_live);
	ClassDB::bind_method(D_METHOD("shake", "strength", "duration"), &VirtualCamera2D::shake, DEFVAL(1.0), DEFVAL(0.5));
	ClassDB::bind_method(D_METHOD("snap"), &VirtualCamera2D::snap);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "priority"), "set_priority", "get_priority");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "zoom", PROPERTY_HINT_LINK), "set_zoom", "get_zoom");
	ADD_GROUP("Follow", "follow_");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "follow_mode", PROPERTY_HINT_ENUM, "None,Target,Group"), "set_follow_mode", "get_follow_mode");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "follow_target", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node2D"), "set_follow_target", "get_follow_target");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "follow_targets", PROPERTY_HINT_ARRAY_TYPE, vformat("%s/%s:%s", Variant::NODE_PATH, PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Node2D")), "set_follow_targets", "get_follow_targets");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "follow_offset", PROPERTY_HINT_NONE, "suffix:px"), "set_follow_offset", "get_follow_offset");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "follow_damping", PROPERTY_HINT_RANGE, "0,5,0.01,or_greater,suffix:s"), "set_follow_damping", "get_follow_damping");
	ADD_GROUP("Group", "group_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "group_margin", PROPERTY_HINT_RANGE, "0,1000,1,or_greater,suffix:px"), "set_group_margin", "get_group_margin");
	ADD_GROUP("Blend", "blend_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "blend_time", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s"), "set_blend_time", "get_blend_time");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "blend_transition", PROPERTY_HINT_ENUM, "Linear,Sine,Quint,Quart,Quad,Expo,Elastic,Cubic,Circ,Bounce,Back,Spring"), "set_blend_transition", "get_blend_transition");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "blend_ease", PROPERTY_HINT_ENUM, "In,Out,In Out,Out In"), "set_blend_ease", "get_blend_ease");

	ADD_SIGNAL(MethodInfo("live_changed", PropertyInfo(Variant::BOOL, "live")));

	BIND_ENUM_CONSTANT(FOLLOW_NONE);
	BIND_ENUM_CONSTANT(FOLLOW_TARGET);
	BIND_ENUM_CONSTANT(FOLLOW_GROUP);
}
