#include "register_types.h"

#include "console.h"
#include "console_view.h"
#include "debug_draw.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

static DebugDraw *debug_draw = nullptr;
static Console *console = nullptr;

void initialize_debug_tools_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(DebugDraw);
		debug_draw = memnew(DebugDraw);
		Engine::get_singleton()->add_singleton(Engine::Singleton("DebugDraw", DebugDraw::get_singleton()));
		GDREGISTER_CLASS(Console);
		GDREGISTER_INTERNAL_CLASS(ConsoleView);
		console = memnew(Console);
		Engine::get_singleton()->add_singleton(Engine::Singleton("Console", Console::get_singleton()));
	}
}

void uninitialize_debug_tools_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		memdelete(console);
		console = nullptr;
		memdelete(debug_draw);
		debug_draw = nullptr;
	}
}
