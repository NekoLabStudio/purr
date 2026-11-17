#include "input_bindings_call.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

static void emit_completed(const Ref<InputBindingsCall> &p_call, const Dictionary &p_result) {
	p_call->emit_signal(SNAME("completed"), p_result);
}

// The deferred call holds the call object, which nothing else may be keeping alive by then.
void InputBindingsCall::complete_later(const Dictionary &p_result) {
	callable_mp_static(&emit_completed).call_deferred(Ref<InputBindingsCall>(this), p_result);
}

void InputBindingsCall::_bind_methods() {
	ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::DICTIONARY, "result")));
}
