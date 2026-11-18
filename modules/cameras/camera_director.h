#pragma once

// CameraDirector: moves each viewport's Camera3D and Camera2D to its live virtual camera, after
// the game's own processing each frame. It joins the tree with the first virtual camera, and
// blends from where the real camera was when the live one changes.

#include "core/templates/hash_map.h"
#include "scene/main/node.h"

class VirtualCamera2D;
class VirtualCamera3D;

class CameraDirector : public Node {
	GDCLASS(CameraDirector, Node);

	struct View {
		ObjectID live;
		ObjectID camera;
		bool blending = false;
		real_t blend_progress = 0.0;
		Transform3D from_3d;
		real_t from_fov = 75.0;
		Vector2 from_position;
		Vector2 from_zoom = Vector2(1, 1);
	};

	static CameraDirector *singleton;
	static Vector<ObjectID> cameras_3d;
	static Vector<ObjectID> cameras_2d;

	HashMap<ObjectID, View> views_3d; // By viewport.
	HashMap<ObjectID, View> views_2d;

	static void _ensure();
	template <typename T>
	static void _end_live(View &p_view);
	void _direct_3d(real_t p_delta);
	void _direct_2d(real_t p_delta);

protected:
	void _notification(int p_what);

public:
	static void add_camera(VirtualCamera3D *p_camera);
	static void remove_camera(VirtualCamera3D *p_camera);
	static void add_camera(VirtualCamera2D *p_camera);
	static void remove_camera(VirtualCamera2D *p_camera);
	static void cleanup();

	CameraDirector();
	~CameraDirector();
};
