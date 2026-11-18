#include "camera_director.h"

#include "virtual_camera_2d.h"
#include "virtual_camera_3d.h"

#include "core/object/callable_mp.h"
#include "scene/2d/camera_2d.h"
#include "scene/3d/camera_3d.h"
#ifndef XR_DISABLED
#include "scene/3d/xr/xr_nodes.h"
#endif
#include "scene/animation/tween.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

CameraDirector *CameraDirector::singleton = nullptr;
Vector<ObjectID> CameraDirector::cameras_3d;
Vector<ObjectID> CameraDirector::cameras_2d;

namespace {
constexpr int LAST = 1 << 30; // Processes after everything a game would order.

template <typename T>
T *best_for(const Vector<ObjectID> &p_cameras, Viewport *p_viewport) {
	T *best = nullptr;
	for (const ObjectID &id : p_cameras) {
		T *camera = Object::cast_to<T>(ObjectDB::get_instance(id));
		if (camera == nullptr || !camera->is_inside_tree() || !camera->can_process() || camera->get_viewport() != p_viewport) {
			continue;
		}
		if (best == nullptr || camera->get_priority() > best->get_priority() || (camera->get_priority() == best->get_priority() && camera->get_ranked_at() > best->get_ranked_at())) {
			best = camera;
		}
	}
	return best;
}

template <typename T>
Vector<Viewport *> viewports_of(const Vector<ObjectID> &p_cameras) {
	Vector<Viewport *> viewports;
	for (const ObjectID &id : p_cameras) {
		T *camera = Object::cast_to<T>(ObjectDB::get_instance(id));
		if (camera != nullptr && camera->is_inside_tree() && !viewports.has(camera->get_viewport())) {
			viewports.push_back(camera->get_viewport());
		}
	}
	return viewports;
}
} // namespace

CameraDirector::CameraDirector() {
	singleton = this;
	set_name("CameraDirector");
	set_process_mode(PROCESS_MODE_ALWAYS);
	set_process_priority(LAST);
}

CameraDirector::~CameraDirector() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

void CameraDirector::_ensure() {
	if (singleton != nullptr) {
		return;
	}
	SceneTree *tree = SceneTree::get_singleton();
	if (tree == nullptr || tree->get_root() == nullptr) {
		return;
	}
	CameraDirector *director = memnew(CameraDirector);
	tree->get_root()->call_deferred(SNAME("add_child"), director, false, Node::INTERNAL_MODE_BACK);
}

void CameraDirector::add_camera(VirtualCamera3D *p_camera) {
	cameras_3d.push_back(p_camera->get_instance_id());
	_ensure();
}

void CameraDirector::remove_camera(VirtualCamera3D *p_camera) {
	cameras_3d.erase(p_camera->get_instance_id());
}

void CameraDirector::add_camera(VirtualCamera2D *p_camera) {
	cameras_2d.push_back(p_camera->get_instance_id());
	_ensure();
}

void CameraDirector::remove_camera(VirtualCamera2D *p_camera) {
	cameras_2d.erase(p_camera->get_instance_id());
}

void CameraDirector::cleanup() {
	cameras_3d.clear();
	cameras_2d.clear();
}

void CameraDirector::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY:
			set_process(true);
			break;
		case NOTIFICATION_PROCESS: {
			const real_t delta = get_process_delta_time();
			_direct_3d(delta);
			_direct_2d(delta);
		} break;
	}
}

// No virtual camera in a viewport is competing any more (all gone, or none can process): the last
// live one isn't live, and the next to take over is told it is.
template <typename T>
void CameraDirector::_end_live(View &p_view) {
	T *previous = Object::cast_to<T>(ObjectDB::get_instance(p_view.live));
	if (previous != nullptr) {
		previous->set_live(false);
	}
	p_view.live = ObjectID();
	p_view.blending = false;
}

void CameraDirector::_direct_3d(real_t p_delta) {
	for (const ObjectID &id : cameras_3d) {
		VirtualCamera3D *virtual_camera = Object::cast_to<VirtualCamera3D>(ObjectDB::get_instance(id));
		if (virtual_camera != nullptr && virtual_camera->is_inside_tree() && virtual_camera->can_process()) {
			virtual_camera->update_pose(p_delta);
		}
	}
	for (Viewport *viewport : viewports_of<VirtualCamera3D>(cameras_3d)) {
		VirtualCamera3D *best = best_for<VirtualCamera3D>(cameras_3d, viewport);
		Camera3D *camera = viewport->get_camera_3d();
		View &view = views_3d[viewport->get_instance_id()];
		if (best == nullptr) {
			_end_live<VirtualCamera3D>(view);
			continue;
		}
		if (camera == nullptr) {
			continue;
		}
#ifndef XR_DISABLED
		// In VR the headset moves the camera.
		if (Object::cast_to<XRCamera3D>(camera) != nullptr) {
			continue;
		}
#endif
		if (camera->get_instance_id() != view.camera) {
			// The virtual cameras move it every frame, so there's nothing to interpolate.
			view.camera = camera->get_instance_id();
			camera->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
		}
		if (best->get_instance_id() != view.live) {
			VirtualCamera3D *previous = Object::cast_to<VirtualCamera3D>(ObjectDB::get_instance(view.live));
			if (previous != nullptr) {
				previous->set_live(false);
			}
			view.blending = view.live.is_valid() && best->get_blend_time() > 0.0;
			view.blend_progress = 0.0;
			view.from_3d = camera->get_global_transform();
			view.from_fov = camera->get_fov();
			view.live = best->get_instance_id();
			best->set_live(true);
		}
		Transform3D pose = best->get_shaken_transform();
		real_t fov = best->get_fov();
		if (view.blending) {
			view.blend_progress = MIN(view.blend_progress + p_delta / best->get_blend_time(), 1.0);
			const real_t weight = Tween::run_equation(best->get_blend_transition(), best->get_blend_ease(), view.blend_progress, 0.0, 1.0, 1.0);
			pose = view.from_3d.interpolate_with(pose, weight);
			fov = Math::lerp(view.from_fov, fov, weight);
			view.blending = view.blend_progress < 1.0;
		}
		camera->set_global_transform(pose);
		camera->set_fov(fov);
	}
}

void CameraDirector::_direct_2d(real_t p_delta) {
	for (const ObjectID &id : cameras_2d) {
		VirtualCamera2D *virtual_camera = Object::cast_to<VirtualCamera2D>(ObjectDB::get_instance(id));
		if (virtual_camera != nullptr && virtual_camera->is_inside_tree() && virtual_camera->can_process()) {
			virtual_camera->update_pose(p_delta);
		}
	}
	for (Viewport *viewport : viewports_of<VirtualCamera2D>(cameras_2d)) {
		VirtualCamera2D *best = best_for<VirtualCamera2D>(cameras_2d, viewport);
		Camera2D *camera = viewport->get_camera_2d();
		View &view = views_2d[viewport->get_instance_id()];
		if (best == nullptr) {
			_end_live<VirtualCamera2D>(view);
			continue;
		}
		if (camera == nullptr) {
			continue;
		}
		if (camera->get_instance_id() != view.camera) {
			view.camera = camera->get_instance_id();
			camera->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
			camera->set_position_smoothing_enabled(false);
		}
		if (best->get_instance_id() != view.live) {
			VirtualCamera2D *previous = Object::cast_to<VirtualCamera2D>(ObjectDB::get_instance(view.live));
			if (previous != nullptr) {
				previous->set_live(false);
			}
			view.blending = view.live.is_valid() && best->get_blend_time() > 0.0;
			view.blend_progress = 0.0;
			view.from_position = camera->get_global_position();
			view.from_zoom = camera->get_zoom();
			view.live = best->get_instance_id();
			best->set_live(true);
		}
		Vector2 position = best->get_shaken_position();
		Vector2 zoom = best->get_current_zoom();
		if (view.blending) {
			view.blend_progress = MIN(view.blend_progress + p_delta / best->get_blend_time(), 1.0);
			const real_t weight = Tween::run_equation(best->get_blend_transition(), best->get_blend_ease(), view.blend_progress, 0.0, 1.0, 1.0);
			position = view.from_position.lerp(position, weight);
			zoom = view.from_zoom.lerp(zoom, weight);
			view.blending = view.blend_progress < 1.0;
		}
		camera->set_global_position(position);
		camera->set_zoom(zoom);
	}
}
