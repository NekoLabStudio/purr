#include "input_prompt.h"

#include "input_bindings.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

InputPrompt::InputPrompt() {
	set_mouse_filter(MOUSE_FILTER_IGNORE);
}

static int prompt_height(int p_height) {
	if (p_height > 0) {
		return p_height;
	}
	// The pixel style is drawn at its own size unless asked otherwise.
	return String(GLOBAL_GET("input_devices/prompts/style")) == "pixel" ? 16 : int(GLOBAL_GET("input_devices/prompts/size"));
}

void InputPrompt::refresh() {
	InputBindings *bindings = InputBindings::get_singleton();
	image.unref();
	text = String();
	if (!bindings || Engine::get_singleton()->is_editor_hint()) {
		text = binding.is_empty() ? String(action) : String();
	} else if (!binding.is_empty()) {
		image = bindings->get_binding_prompt(binding, player, prompt_height(height) * 2);
		text = image.is_null() ? bindings->describe_binding(binding) : String();
	} else if (action != StringName()) {
		image = bindings->get_prompt(action, player, prompt_height(height) * 2);
		text = image.is_null() ? bindings->describe(action, player) : String();
	}
	if (String(GLOBAL_GET("input_devices/prompts/style")) == "pixel") {
		set_texture_filter(TEXTURE_FILTER_NEAREST);
	}
	update_minimum_size();
	queue_redraw();
}

void InputPrompt::player_family_changed(int p_player, int p_family) {
	if (p_player == player) {
		refresh();
	}
}

void InputPrompt::bindings_changed(int p_player) {
	if (p_player == player) {
		refresh();
	}
}

Size2 InputPrompt::get_minimum_size() const {
	const float tall = prompt_height(height);
	if (image.is_valid()) {
		return Size2(tall * image->get_width() / MAX(image->get_height(), 1), tall);
	}
	const Ref<Font> font = get_theme_font(SNAME("font"), SNAME("Label"));
	const int size = get_theme_font_size(SNAME("font_size"), SNAME("Label"));
	return font.is_valid() ? font->get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, size) : Size2();
}

void InputPrompt::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			InputBindings *bindings = InputBindings::get_singleton();
			if (bindings && !Engine::get_singleton()->is_editor_hint()) {
				bindings->connect(SNAME("player_family_changed"), callable_mp(this, &InputPrompt::player_family_changed));
				bindings->connect(SNAME("bindings_changed"), callable_mp(this, &InputPrompt::bindings_changed));
				bindings->connect(SNAME("devices_changed"), callable_mp(this, &InputPrompt::refresh));
			}
			refresh();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			InputBindings *bindings = InputBindings::get_singleton();
			if (bindings && bindings->is_connected(SNAME("player_family_changed"), callable_mp(this, &InputPrompt::player_family_changed))) {
				bindings->disconnect(SNAME("player_family_changed"), callable_mp(this, &InputPrompt::player_family_changed));
				bindings->disconnect(SNAME("bindings_changed"), callable_mp(this, &InputPrompt::bindings_changed));
				bindings->disconnect(SNAME("devices_changed"), callable_mp(this, &InputPrompt::refresh));
			}
		} break;
		case NOTIFICATION_THEME_CHANGED: {
			update_minimum_size();
		} break;
		case NOTIFICATION_DRAW: {
			if (image.is_valid()) {
				const Size2 tall = get_minimum_size();
				draw_texture_rect(image, Rect2((get_size() - tall) / 2, tall));
			} else if (!text.is_empty()) {
				const Ref<Font> font = get_theme_font(SNAME("font"), SNAME("Label"));
				const int size = get_theme_font_size(SNAME("font_size"), SNAME("Label"));
				const Color color = get_theme_color(SNAME("font_color"), SNAME("Label"));
				const float ascent = font->get_ascent(size);
				const Size2 extent = font->get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, size);
				draw_string(font, Point2((get_size().x - extent.x) / 2, (get_size().y - extent.y) / 2 + ascent), text, HORIZONTAL_ALIGNMENT_LEFT, -1, size, color);
			}
		} break;
	}
}

void InputPrompt::set_action(const StringName &p_action) {
	action = p_action;
	if (is_inside_tree()) {
		refresh();
	}
}

void InputPrompt::set_player(int p_player) {
	player = p_player;
	if (is_inside_tree()) {
		refresh();
	}
}

void InputPrompt::set_binding(const Dictionary &p_binding) {
	binding = p_binding;
	if (is_inside_tree()) {
		refresh();
	}
}

void InputPrompt::set_height(int p_height) {
	height = p_height;
	if (is_inside_tree()) {
		refresh();
	}
}

void InputPrompt::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_action", "action"), &InputPrompt::set_action);
	ClassDB::bind_method(D_METHOD("get_action"), &InputPrompt::get_action);
	ClassDB::bind_method(D_METHOD("set_player", "player"), &InputPrompt::set_player);
	ClassDB::bind_method(D_METHOD("get_player"), &InputPrompt::get_player);
	ClassDB::bind_method(D_METHOD("set_binding", "binding"), &InputPrompt::set_binding);
	ClassDB::bind_method(D_METHOD("get_binding"), &InputPrompt::get_binding);
	ClassDB::bind_method(D_METHOD("set_height", "height"), &InputPrompt::set_height);
	ClassDB::bind_method(D_METHOD("get_height"), &InputPrompt::get_height);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "action", PROPERTY_HINT_INPUT_NAME), "set_action", "get_action");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "player", PROPERTY_HINT_RANGE, "0,7,1"), "set_player", "get_player");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "binding"), "set_binding", "get_binding");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "height", PROPERTY_HINT_RANGE, "0,256,1,suffix:px"), "set_height", "get_height");
}
