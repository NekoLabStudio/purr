#pragma once

// An internal node at the end of the scene tree's root, where it sees every input event before
// the game's nodes do (Godot hands input to the last node first), and passes them to
// InputBindings: capturing a binding, telling a wheel's hat from its buttons, and noting which
// controller each player used last.

#include "scene/main/node.h"

class InputBindingsListener : public Node {
	GDCLASS(InputBindingsListener, Node);

protected:
	static void _bind_methods() {}

public:
	virtual void input(const Ref<InputEvent> &p_event) override;

	InputBindingsListener();
};
