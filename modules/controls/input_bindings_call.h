#pragma once

// What an awaited InputBindings call returns the signal of: its completed signal carries the
// result, a Dictionary with ok (and error and message when it failed).

#include "core/object/ref_counted.h"

class InputBindingsCall : public RefCounted {
	GDCLASS(InputBindingsCall, RefCounted);

protected:
	static void _bind_methods();

public:
	// Completes on the next idle frame, so an awaiting caller is connected first.
	void complete_later(const Dictionary &p_result);
};
