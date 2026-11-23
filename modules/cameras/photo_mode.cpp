#include "photo_mode.h"

#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/time.h"
#include "scene/3d/camera_3d.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/label.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/slider.h"
#include "scene/main/canvas_layer.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "scene/resources/camera_attributes.h"

#include "camera_focus.h"
#include "scene/resources/compositor.h"
#include "servers/rendering/rendering_server.h"

#include "photo_mode_view.h"

PhotoMode *PhotoMode::singleton = nullptr;

namespace {
constexpr float LOOK_SPEED = 0.004; // Radians a pixel of mouse movement turns.
constexpr float STICK_LOOK_SPEED = 2.0; // Radians a second at full tilt.
constexpr float MOVE_SPEED = 4.0; // Metres a second; three times that holding Shift.
constexpr float STICK_DEAD_ZONE = 0.15;

void complete_shot(const Ref<PhotoModeShot> &p_shot, const Dictionary &p_result) {
	p_shot->emit_signal(SNAME("completed"), p_result);
}

float stick(JoyAxis p_axis) {
	const float value = Input::get_singleton()->get_joy_axis(0, p_axis);
	return Math::abs(value) < STICK_DEAD_ZONE ? 0.0f : value;
}
} // namespace

PhotoMode::PhotoMode() {
	singleton = this;
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "photo_mode/max_distance", PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater,suffix:m"), 20.0);
	GLOBAL_DEF("photo_mode/photos_folder", "user://photos");
}

PhotoMode::~PhotoMode() {
	singleton = nullptr;
}

Viewport *PhotoMode::_root() {
	SceneTree *tree = SceneTree::get_singleton();
	return tree != nullptr ? tree->get_root() : nullptr;
}

bool PhotoMode::is_open() const {
	return holder != nullptr;
}

bool PhotoMode::open() {
	if (is_open()) {
		return true;
	}
	Viewport *root = _root();
	ERR_FAIL_NULL_V(root, false);
	Camera3D *game = root->get_camera_3d();
	ERR_FAIL_NULL_V_MSG(game, false, "Photo mode needs the game's window to have a 3D camera.");
	game_camera = game->get_instance_id();

	// The game stops, and its HUD goes.
	SceneTree *tree = SceneTree::get_singleton();
	was_paused = tree->is_paused();
	tree->set_pause(true);
	for (int i = 0; i < root->get_child_count(); i++) {
		Node *child = root->get_child(i);
		CanvasLayer *layer_node = Object::cast_to<CanvasLayer>(child);
		CanvasItem *item = Object::cast_to<CanvasItem>(child);
		if (layer_node != nullptr && layer_node->is_visible()) {
			layer_node->hide();
			hidden.push_back(layer_node->get_instance_id());
		} else if (item != nullptr && item->is_visible()) {
			item->hide();
			hidden.push_back(item->get_instance_id());
		}
	}

	holder = memnew(PhotoModeView(callable_mp(this, &PhotoMode::_process), callable_mp(this, &PhotoMode::_input)));
	holder->set_name("PhotoMode");
	root->add_child(holder, false, Node::INTERNAL_MODE_BACK);

	// A camera of our own where the game's was, seeing as it saw.
	camera = memnew(Camera3D);
	camera->set_cull_mask(game->get_cull_mask());
	camera->set_environment(game->get_environment());
	camera->set_near(game->get_near());
	camera->set_far(game->get_far());
	camera->set_physics_interpolation_mode(Node::PHYSICS_INTERPOLATION_MODE_OFF);
	const Ref<CameraAttributesPractical> game_attributes = game->get_attributes();
	attributes = game_attributes.is_valid() ? Ref<CameraAttributesPractical>(game_attributes->duplicate()) : Ref<CameraAttributesPractical>(memnew(CameraAttributesPractical));
	camera->set_attributes(attributes);
	compositor = game->get_compositor().is_valid() ? Ref<Compositor>(game->get_compositor()->duplicate()) : Ref<Compositor>(memnew(Compositor));
	camera->set_compositor(compositor);
	holder->add_child(camera);
	const Transform3D from = game->get_global_transform();
	camera->set_global_transform(from);
	camera->set_fov(game->get_fov());
	start = from.origin;
	const Vector3 forward = -from.basis.get_column(Vector3::AXIS_Z);
	yaw = Math::atan2(-forward.x, -forward.z);
	pitch = Math::asin(CLAMP(forward.y, -1.0f, 1.0f));
	camera->make_current();

	_build_panel();
	emit_signal(SNAME("opened"));
	return true;
}

void PhotoMode::close() {
	if (!is_open()) {
		return;
	}
	for (const ObjectID &id : hidden) {
		Node *node = ObjectDB::get_instance<Node>(id);
		if (CanvasLayer *layer_node = Object::cast_to<CanvasLayer>(node)) {
			layer_node->show();
		} else if (CanvasItem *item = Object::cast_to<CanvasItem>(node)) {
			item->show();
		}
	}
	hidden.clear();
	Camera3D *game = ObjectDB::get_instance<Camera3D>(game_camera);
	if (game != nullptr && game->is_inside_tree()) {
		game->make_current();
	}
	holder->queue_free();
	holder = nullptr;
	camera = nullptr;
	layer = nullptr;
	panel = nullptr;
	attributes.unref();
	compositor.unref();
	SceneTree::get_singleton()->set_pause(was_paused);
	if (shot.is_valid()) {
		Dictionary result;
		result["ok"] = false;
		result["error"] = "closed";
		result["message"] = "Photo mode closed before the photo was taken.";
		Ref<PhotoModeShot> done = shot;
		shot.unref();
		callable_mp_static(&complete_shot).call_deferred(done, result);
	}
	emit_signal(SNAME("closed"));
}

HSlider *PhotoMode::_add_slider(Control *p_box, const String &p_label, double p_min, double p_max, double p_step, double p_value) {
	Label *label = memnew(Label);
	label->set_text(p_label);
	p_box->add_child(label);
	HSlider *slider = memnew(HSlider);
	slider->set_min(p_min);
	slider->set_max(p_max);
	slider->set_step(p_step);
	slider->set_value(p_value);
	slider->set_custom_minimum_size(Size2(240, 0));
	slider->connect(SNAME("value_changed"), callable_mp(this, &PhotoMode::_settings_changed));
	p_box->add_child(slider);
	return slider;
}

void PhotoMode::_build_panel() {
	layer = memnew(CanvasLayer);
	layer->set_layer(128);
	holder->add_child(layer);
	PanelContainer *container = memnew(PanelContainer);
	container->set_anchors_and_offsets_preset(Control::PRESET_BOTTOM_LEFT, Control::PRESET_MODE_MINSIZE, 24);
	container->set_grow_direction_preset(Control::PRESET_BOTTOM_LEFT);
	layer->add_child(container);
	panel = container;
	VBoxContainer *box = memnew(VBoxContainer);
	container->add_child(box);

	fov_slider = _add_slider(box, holder->atr("Field of view"), 10, 120, 1, camera->get_fov());
	roll_slider = _add_slider(box, holder->atr("Roll"), -45, 45, 1, 0);
	focus_slider = _add_slider(box, holder->atr("Focus distance"), 0.5, 100, 0.1, 10);
	blur_slider = _add_slider(box, holder->atr("Background blur"), 0, 1, 0.01, 0);
	exposure_slider = _add_slider(box, holder->atr("Exposure"), -2, 2, 0.05, 0);
	vignette_box = memnew(CheckBox);
	vignette_box->set_text(holder->atr("Vignette"));
	vignette_box->connect(SNAME("toggled"), callable_mp(this, &PhotoMode::_toggled));
	box->add_child(vignette_box);
	grain_box = memnew(CheckBox);
	grain_box->set_text(holder->atr("Film grain"));
	grain_box->connect(SNAME("toggled"), callable_mp(this, &PhotoMode::_toggled));
	box->add_child(grain_box);
	HBoxContainer *buttons = memnew(HBoxContainer);
	box->add_child(buttons);
	Button *take = memnew(Button);
	take->set_text(holder->atr("Take photo"));
	take->connect(SNAME("pressed"), callable_mp(this, &PhotoMode::take_photo));
	buttons->add_child(take);
	Button *leave = memnew(Button);
	leave->set_text(holder->atr("Close"));
	leave->connect(SNAME("pressed"), callable_mp(this, &PhotoMode::close));
	buttons->add_child(leave);
	Label *keys = memnew(Label);
	keys->set_text(holder->atr("Right mouse button or right stick to look, WASD or left stick to move, H to hide this"));
	keys->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	keys->set_custom_minimum_size(Size2(240, 0));
	keys->set_modulate(Color(1, 1, 1, 0.6));
	box->add_child(keys);
	_settings_changed();
}

// The panel's settings, on our camera, its attributes and its compositor's effects.
void PhotoMode::_settings_changed(double p_value) {
	if (camera == nullptr || panel == nullptr) {
		return;
	}
	camera->set_fov(fov_slider->get_value());
	const float focus = focus_slider->get_value();
	const float blur = blur_slider->get_value();
	CameraFocus::set(attributes.ptr(), focus, blur);
	attributes->set_exposure_multiplier(Math::pow(2.0, exposure_slider->get_value()));

	// The photo's own vignette and grain, beside whatever the game's compositor has.
	TypedArray<CompositorEffect> effects = compositor->get_compositor_effects();
	TypedArray<CompositorEffect> kept;
	for (int i = 0; i < effects.size(); i++) {
		Ref<CompositorEffect> effect = effects[i];
		if (effect.is_valid() && !effect->has_meta(SNAME("_photo_mode"))) {
			kept.push_back(effect);
		}
	}
	for (const char *name : { "VignetteEffect", "FilmGrainEffect" }) {
		const bool wanted = String(name) == "VignetteEffect" ? vignette_box->is_pressed() : grain_box->is_pressed();
		if (wanted && ClassDB::class_exists(name)) {
			Ref<CompositorEffect> effect = Object::cast_to<CompositorEffect>(ClassDB::instantiate(name));
			if (effect.is_valid()) {
				effect->set_meta(SNAME("_photo_mode"), true);
				kept.push_back(effect);
			}
		}
	}
	compositor->set_compositor_effects(kept);
}

void PhotoMode::_input(const Ref<InputEvent> &p_event) {
	const Ref<InputEventMouseMotion> motion = p_event;
	if (motion.is_valid() && Input::get_singleton()->is_mouse_button_pressed(MouseButton::RIGHT)) {
		yaw -= motion->get_relative().x * LOOK_SPEED;
		pitch = CLAMP(pitch - motion->get_relative().y * LOOK_SPEED, -1.5f, 1.5f);
	}
	const Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
		if (key->get_keycode() == Key::ESCAPE) {
			close();
		} else if (key->get_keycode() == Key::H && panel != nullptr) {
			panel->set_visible(!panel->is_visible());
		}
	}
	const Ref<InputEventJoypadButton> button = p_event;
	if (button.is_valid() && button->is_pressed()) {
		if (button->get_button_index() == JoyButton::B || button->get_button_index() == JoyButton::START) {
			close();
		} else if (button->get_button_index() == JoyButton::A) {
			take_photo();
		} else if (button->get_button_index() == JoyButton::Y && panel != nullptr) {
			panel->set_visible(!panel->is_visible());
		}
	}
}

void PhotoMode::_process(double p_delta) {
	if (camera == nullptr) {
		return;
	}
	if (shot.is_valid() && ++shot_frames >= 3) {
		_take_now();
		if (camera == nullptr) {
			return; // Closed by whoever was waiting for the photo.
		}
	}
	Input *input = Input::get_singleton();
	yaw -= stick(JoyAxis::RIGHT_X) * STICK_LOOK_SPEED * p_delta;
	pitch = CLAMP(pitch - stick(JoyAxis::RIGHT_Y) * STICK_LOOK_SPEED * float(p_delta), -1.5f, 1.5f);
	Vector3 move(stick(JoyAxis::LEFT_X), 0.0, stick(JoyAxis::LEFT_Y));
	move.x += float(input->is_physical_key_pressed(Key::D)) - float(input->is_physical_key_pressed(Key::A));
	move.z += float(input->is_physical_key_pressed(Key::S)) - float(input->is_physical_key_pressed(Key::W));
	move.y += float(input->is_physical_key_pressed(Key::E)) - float(input->is_physical_key_pressed(Key::Q));
	move.y += input->get_joy_axis(0, JoyAxis::TRIGGER_RIGHT) - input->get_joy_axis(0, JoyAxis::TRIGGER_LEFT);
	const float speed = MOVE_SPEED * (input->is_physical_key_pressed(Key::SHIFT) ? 3.0f : 1.0f);

	const Basis turn = Basis(Vector3(0, 1, 0), yaw) * Basis(Vector3(1, 0, 0), pitch) * Basis(Vector3(0, 0, 1), Math::deg_to_rad(float(roll_slider->get_value())));
	Vector3 at = camera->get_global_position() + turn.xform(Vector3(move.x, 0, move.z)) * speed * p_delta + Vector3(0, move.y, 0) * speed * p_delta;
	// Kept within reach of where the game's camera was, so the player can't see past the level.
	const float reach = GLOBAL_GET("photo_mode/max_distance");
	if (reach > 0.0 && at.distance_to(start) > reach) {
		at = start + (at - start).normalized() * reach;
	}
	camera->set_global_transform(Transform3D(turn, at));
}

Signal PhotoMode::take_photo() {
	if (!is_open()) {
		Ref<PhotoModeShot> refused;
		refused.instantiate();
		Dictionary result;
		result["ok"] = false;
		result["error"] = "not_open";
		result["message"] = "Photo mode isn't open: call PhotoMode.open() first.";
		callable_mp_static(&complete_shot).call_deferred(refused, result);
		return Signal(refused.ptr(), SNAME("completed"));
	}
	if (shot.is_null()) {
		shot.instantiate();
		shot_frames = 0;
		if (panel != nullptr) {
			panel_was_shown = panel->is_visible();
			panel->hide(); // Out of the picture, for the frames it takes to draw without it.
		}
	}
	return Signal(shot.ptr(), SNAME("completed"));
}

void PhotoMode::_take_now() {
	Ref<PhotoModeShot> done = shot;
	shot.unref();
	Dictionary result;
	const Ref<Image> image = _root()->get_texture()->get_image();
	const String folder = GLOBAL_GET("photo_mode/photos_folder");
	DirAccess::make_dir_recursive_absolute(folder);
	// Named for the second it was taken, numbered after the first in the same second.
	const String stamp = "photo-" + Time::get_singleton()->get_datetime_string_from_system().replace_char(':', '-').replace_char('T', '-');
	String path = folder.path_join(stamp + ".png");
	for (int n = 2; FileAccess::exists(path); n++) {
		path = folder.path_join(vformat("%s-%d.png", stamp, n));
	}
	const Error err = image.is_valid() ? image->save_png(path) : ERR_CANT_CREATE;
	if (panel != nullptr) {
		panel->set_visible(panel_was_shown);
	}
	result["ok"] = err == OK;
	if (err == OK) {
		result["path"] = path;
		emit_signal(SNAME("photo_taken"), path);
	} else {
		result["error"] = "not_saved";
		result["message"] = vformat("The photo couldn't be saved to %s.", path);
	}
	// After this frame's work: whoever awaits it may close photo mode at once.
	callable_mp_static(&complete_shot).call_deferred(done, result);
}

void PhotoMode::_bind_methods() {
	ClassDB::bind_method(D_METHOD("open"), &PhotoMode::open);
	ClassDB::bind_method(D_METHOD("close"), &PhotoMode::close);
	ClassDB::bind_method(D_METHOD("is_open"), &PhotoMode::is_open);
	ClassDB::bind_method(D_METHOD("take_photo"), &PhotoMode::take_photo);

	ADD_SIGNAL(MethodInfo("opened"));
	ADD_SIGNAL(MethodInfo("closed"));
	ADD_SIGNAL(MethodInfo("photo_taken", PropertyInfo(Variant::STRING, "path")));
}
