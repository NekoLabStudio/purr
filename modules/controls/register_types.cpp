#include "register_types.h"

#include "input_bindings.h"
#include "input_bindings_call.h"
#include "input_bindings_listener.h"
#include "input_prompt.h"
#include "input_prompt_set.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

static InputBindings *input_bindings = nullptr;

void initialize_controls_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(InputBindings);
		GDREGISTER_INTERNAL_CLASS(InputBindingsCall);
		GDREGISTER_INTERNAL_CLASS(InputBindingsListener);
		GDREGISTER_CLASS(InputPrompt);
		GDREGISTER_CLASS(InputPromptSet);
		input_bindings = memnew(InputBindings);
		Engine::get_singleton()->add_singleton(Engine::Singleton("InputBindings", InputBindings::get_singleton()));
	}
}

void uninitialize_controls_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		memdelete(input_bindings);
		input_bindings = nullptr;
	}
}
