#pragma once

// The console on screen: output above a line to type in, across the top of the screen, above
// everything else. Opens and closes with the console's key; Escape closes it too. Up and Down go
// through earlier commands, Tab completes a command's name.

#include "scene/main/canvas_layer.h"

class LineEdit;
class PanelContainer;
class RichTextLabel;

class ConsoleView : public CanvasLayer {
	GDCLASS(ConsoleView, CanvasLayer);

	PanelContainer *panel = nullptr;
	RichTextLabel *output_label = nullptr;
	LineEdit *line = nullptr;
	int shown_serial = 0;
	int history_index = -1; // -1: typing a new line.
	int mouse_mode_before = 0;
	bool input_disabled_before = false;

	void _build();
	void _show_new_output();
	void _submitted(const String &p_text);
	void _input_gui_input(const Ref<InputEvent> &p_event);

protected:
	void _notification(int p_what);
	virtual void input(const Ref<InputEvent> &p_event) override;

public:
	void set_open(bool p_open);
	void clear_output();

	ConsoleView();
};
