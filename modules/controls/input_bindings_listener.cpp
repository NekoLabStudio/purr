#include "input_bindings_listener.h"

#include "input_bindings.h"

#include "scene/main/viewport.h"

void InputBindingsListener::input(const Ref<InputEvent> &p_event) {
	InputBindings *bindings = InputBindings::get_singleton();
	if (bindings && bindings->input(p_event)) {
		get_viewport()->set_input_as_handled();
	}
}

InputBindingsListener::InputBindingsListener() {
	set_process_mode(PROCESS_MODE_ALWAYS);
	set_process_input(true);
}
