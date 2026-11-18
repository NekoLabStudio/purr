#include "console_view.h"

#include "console.h"

#include "core/input/input.h"
#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "core/os/keyboard.h"
#include "scene/gui/box_container.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/rich_text_label.h"
#include "scene/resources/font.h"
#include "scene/resources/style_box_flat.h"

namespace {
constexpr int LAYER = 1 << 20; // Over everything, DebugDraw's labels and values too.
constexpr float HEIGHT = 0.4; // Of the screen.
constexpr int FONT_SIZE = 14;
} // namespace

ConsoleView::ConsoleView() {
	set_name("Console");
	set_layer(LAYER);
	set_process_mode(PROCESS_MODE_ALWAYS);
	set_visible(false);
}

void ConsoleView::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY:
			set_process_input(true);
			break;
		case NOTIFICATION_PROCESS:
			_show_new_output();
			break;
	}
}

void ConsoleView::_build() {
	if (panel != nullptr) {
		return;
	}
	Ref<SystemFont> mono;
	mono.instantiate();
	mono->set_font_names({ "Menlo", "SF Mono", "Consolas", "DejaVu Sans Mono", "monospace" });

	Ref<StyleBoxFlat> background;
	background.instantiate();
	background->set_bg_color(Color(0.08, 0.085, 0.1, 0.94));
	background->set_content_margin_all(10);

	panel = memnew(PanelContainer);
	panel->add_theme_style_override(SNAME("panel"), background);
	panel->set_anchors_preset(Control::PRESET_TOP_WIDE);
	panel->set_anchor(SIDE_BOTTOM, HEIGHT);
	add_child(panel);

	VBoxContainer *column = memnew(VBoxContainer);
	panel->add_child(column);

	output_label = memnew(RichTextLabel);
	output_label->set_scroll_follow(true);
	output_label->set_selection_enabled(true);
	output_label->set_context_menu_enabled(true);
	output_label->set_focus_mode(Control::FOCUS_NONE);
	output_label->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	output_label->add_theme_font_override(SNAME("normal_font"), mono);
	output_label->add_theme_font_size_override(SNAME("normal_font_size"), FONT_SIZE);
	column->add_child(output_label);

	line = memnew(LineEdit);
	line->set_keep_editing_on_text_submit(true);
	line->set_placeholder("Type help to see the commands");
	line->add_theme_font_override(SNAME("font"), mono);
	line->add_theme_font_size_override(SNAME("font_size"), FONT_SIZE);
	line->connect(SNAME("text_submitted"), callable_mp(this, &ConsoleView::_submitted));
	line->connect(SNAME("gui_input"), callable_mp(this, &ConsoleView::_input_gui_input));
	column->add_child(line);

	shown_serial = 0;
}

// Lines written since the last look, appended (or everything, after a clear or when lines fell
// off the top). Colour says what a line is: what was typed is dim, warnings amber, errors red.
void ConsoleView::_show_new_output() {
	const Console *console = Console::get_singleton();
	if (console == nullptr || output_label == nullptr || shown_serial == console->get_output_serial()) {
		return;
	}
	const Vector<Console::Line> &output = console->get_output();
	const int added = console->get_output_serial() - shown_serial;
	int from = output.size() - added;
	if (from < 0) {
		output_label->clear();
		from = 0;
	}
	for (int i = from; i < output.size(); i++) {
		const Console::Line &line = output[i];
		switch (line.kind) {
			case Console::LINE_NORMAL:
				output_label->push_color(Color(0.92, 0.93, 0.95));
				break;
			case Console::LINE_COMMAND:
				output_label->push_color(Color(0.55, 0.57, 0.62));
				break;
			case Console::LINE_WARNING:
				output_label->push_color(Color(1.0, 0.8, 0.4));
				break;
			case Console::LINE_ERROR:
				output_label->push_color(Color(1.0, 0.45, 0.45));
				break;
		}
		output_label->add_text(line.text + "\n");
		output_label->pop();
	}
	// Lines that fell off the console's top go from the screen too (the last paragraph is the
	// empty one after the final line break).
	while (output_label->get_paragraph_count() > Console::MAX_OUTPUT_LINES + 1) {
		output_label->remove_paragraph(0);
	}
	shown_serial = console->get_output_serial();
}

void ConsoleView::clear_output() {
	if (output_label != nullptr) {
		output_label->clear();
		shown_serial = Console::get_singleton()->get_output_serial();
	}
}

void ConsoleView::set_open(bool p_open) {
	if (p_open == is_visible()) {
		return;
	}
	Input *keys = Input::get_singleton();
	if (p_open) {
		_build();
		set_visible(true);
		set_process(true);
		_show_new_output();
		history_index = -1;
		line->grab_focus();
		// A captured or hidden pointer comes back while the console is open.
		mouse_mode_before = keys->get_mouse_mode();
		keys->set_mouse_mode(Input::MouseMode::MOUSE_MODE_VISIBLE);
		// What's typed is the console's: a game asking whether a key or action is held hears no.
		// Events still come, so the console's line gets them.
		input_disabled_before = keys->is_input_disabled();
		keys->set_disable_input(true);
	} else {
		line->release_focus();
		set_visible(false);
		set_process(false);
		keys->set_mouse_mode(Input::MouseMode(mouse_mode_before));
		keys->set_disable_input(input_disabled_before);
	}
}

void ConsoleView::input(const Ref<InputEvent> &p_event) {
	Console *console = Console::get_singleton();
	const Ref<InputEventKey> key = p_event;
	if (console == nullptr || key.is_null() || !key->is_pressed() || key->is_echo()) {
		return;
	}
	const Key pressed = key->get_physical_keycode() != Key::NONE ? key->get_physical_keycode() : key->get_keycode();
	if (pressed == console->get_open_key()) {
		console->set_visible(!console->is_visible());
		Node::get_viewport()->set_input_as_handled();
	} else if (pressed == Key::ESCAPE && console->is_visible()) {
		console->set_visible(false);
		Node::get_viewport()->set_input_as_handled();
	}
}

void ConsoleView::_submitted(const String &p_text) {
	line->clear();
	history_index = -1;
	Console::get_singleton()->run(p_text);
	_show_new_output();
}

void ConsoleView::_input_gui_input(const Ref<InputEvent> &p_event) {
	const Ref<InputEventKey> key = p_event;
	if (key.is_null() || !key->is_pressed()) {
		return;
	}
	const Console *console = Console::get_singleton();
	const Vector<String> &history = console->get_history();
	switch (key->get_keycode()) {
		case Key::UP:
		case Key::DOWN: {
			if (history.is_empty()) {
				break;
			}
			if (key->get_keycode() == Key::UP) {
				history_index = history_index == -1 ? history.size() - 1 : MAX(history_index - 1, 0);
			} else if (history_index != -1) {
				history_index = history_index + 1 < history.size() ? history_index + 1 : -1;
			}
			line->set_text(history_index == -1 ? String() : history[history_index]);
			line->set_caret_column(line->get_text().length());
			line->accept_event();
		} break;
		case Key::TAB: {
			// The longest start every matching command shares; the matches listed when it's
			// still ambiguous.
			const String typed = line->get_text();
			if (typed.contains_char(' ')) {
				line->accept_event();
				break;
			}
			const PackedStringArray matches = console->complete(typed);
			if (!matches.is_empty()) {
				String common = matches[0];
				for (const String &match : matches) {
					int same = 0;
					while (same < common.length() && same < match.length() && common[same] == match[same]) {
						same++;
					}
					common = common.substr(0, same);
				}
				if (matches.size() == 1) {
					common += " ";
				} else if (common == typed) {
					Console::get_singleton()->write(String("  ").join(matches));
				}
				line->set_text(common);
				line->set_caret_column(common.length());
			}
			line->accept_event();
		} break;
		default:
			break;
	}
}
