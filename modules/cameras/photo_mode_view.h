#pragma once

// PhotoMode's node in the scene: it runs while the game is paused and passes the frames and the
// player's input to PhotoMode, and is where its camera and panel live.

#include "scene/main/node.h"

class PhotoModeView : public Node {
	GDCLASS(PhotoModeView, Node);

	Callable on_process;
	Callable on_input;

protected:
	void _notification(int p_what) {
		if (p_what == NOTIFICATION_PROCESS && on_process.is_valid()) {
			on_process.call(get_process_delta_time());
		}
	}
	virtual void input(const Ref<InputEvent> &p_event) override {
		if (on_input.is_valid()) {
			on_input.call(p_event);
		}
	}

public:
	PhotoModeView(const Callable &p_process = Callable(), const Callable &p_input = Callable()) :
			on_process(p_process), on_input(p_input) {
		set_process_mode(PROCESS_MODE_ALWAYS);
		set_process(true);
		set_process_input(true);
	}
};
