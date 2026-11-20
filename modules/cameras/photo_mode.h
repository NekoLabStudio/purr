#pragma once

// PhotoMode: the game stops, its HUD goes, and the player flies a camera round the scene from where
// the game's camera was, with the field of view, roll, focus, blur, exposure and a vignette and
// grain to set, and takes pictures, saved to user://photos. Games open it from their pause menu.

#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/templates/local_vector.h"

class Camera3D;
class CameraAttributesPractical;
class CanvasLayer;
class CheckBox;
class Compositor;
class Control;
class HSlider;
class Node;
class Viewport;

// A photo being taken: completed with its result once it's saved.
class PhotoModeShot : public RefCounted {
	GDCLASS(PhotoModeShot, RefCounted);

protected:
	static void _bind_methods() {
		ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::DICTIONARY, "result")));
	}
};

class PhotoMode : public Object {
	GDCLASS(PhotoMode, Object);

	static PhotoMode *singleton;

	Node *holder = nullptr; // Ours, in the root, processing while the game is paused.
	Camera3D *camera = nullptr;
	Ref<CameraAttributesPractical> attributes;
	Ref<Compositor> compositor;
	CanvasLayer *layer = nullptr;
	Control *panel = nullptr;
	HSlider *fov_slider = nullptr;
	HSlider *roll_slider = nullptr;
	HSlider *focus_slider = nullptr;
	HSlider *blur_slider = nullptr;
	HSlider *exposure_slider = nullptr;
	CheckBox *vignette_box = nullptr;
	CheckBox *grain_box = nullptr;

	ObjectID game_camera;
	Vector3 start;
	float yaw = 0.0;
	float pitch = 0.0;
	bool was_paused = false;
	LocalVector<ObjectID> hidden; // The game's HUD, hidden while open.
	Ref<PhotoModeShot> shot; // The photo being taken.
	int shot_frames = 0;
	bool panel_was_shown = true; // As the player left it when the photo was taken.

	void _build_panel();
	HSlider *_add_slider(Control *p_box, const String &p_label, double p_min, double p_max, double p_step, double p_value);
	void _settings_changed(double p_value = 0.0);
	void _toggled(bool p_on) { _settings_changed(); }
	void _process(double p_delta);
	void _input(const Ref<class InputEvent> &p_event);
	void _take_now();
	static Viewport *_root();

protected:
	static void _bind_methods();

public:
	static PhotoMode *get_singleton() { return singleton; }

	bool open();
	void close();
	bool is_open() const;
	Signal take_photo();
	bool is_photo_camera(const Camera3D *p_camera) const { return p_camera != nullptr && p_camera == camera; }

	PhotoMode();
	~PhotoMode();
};
