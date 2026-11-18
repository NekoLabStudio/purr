#include "register_types.h"

#include "options.h"
#include "options_watcher.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

static Options *options = nullptr;

void initialize_options_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_CORE) {
		// Before the window opens, so it opens as the player left it.
		Options::apply_before_window();
	}
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(Options);
		GDREGISTER_INTERNAL_CLASS(OptionsWatcher);
		options = memnew(Options);
		Engine::get_singleton()->add_singleton(Engine::Singleton("Options", Options::get_singleton()));
	}
}

void uninitialize_options_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		options->finish();
		memdelete(options);
		options = nullptr;
	}
}
