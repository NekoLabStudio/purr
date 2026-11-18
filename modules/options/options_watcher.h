#pragma once

// OptionsWatcher: has Options write what's changed as a phone, tablet or Apple TV sends the game
// to the background, where the system may close it without warning (only nodes hear of it), and
// check a few times a second whether the window went fullscreen or back another way.

#include "options.h"

#include "core/os/os.h"
#include "scene/main/node.h"

class OptionsWatcher : public Node {
	GDCLASS(OptionsWatcher, Node);

	static constexpr uint64_t CHECK_USEC = 250000;
	uint64_t last_check_usec = 0;

protected:
	void _notification(int p_what) {
		Options *options = Options::get_singleton();
		if (options == nullptr) {
			return;
		}
		switch (p_what) {
			case NOTIFICATION_READY:
				set_process_internal(true);
				break;
			case NOTIFICATION_INTERNAL_PROCESS:
				// By the clock, not the game's time, which the game may slow or stop.
				if (OS::get_singleton()->get_ticks_usec() - last_check_usec >= CHECK_USEC) {
					last_check_usec = OS::get_singleton()->get_ticks_usec();
					options->window_mode_check();
				}
				break;
			case NOTIFICATION_APPLICATION_PAUSED:
				options->finish();
				break;
		}
	}
};
