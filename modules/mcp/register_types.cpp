#include "register_types.h"

#include "editor/mcp_editor_plugin.h"
#include "mcp_capture.h"
#include "mcp_docs.h"
#include "mcp_game.h"
#include "mcp_server.h"
#include "mcp_startup.h"
#include "mcp_tool.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "editor/plugins/editor_plugin.h"

void initialize_mcp_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_INTERNAL_CLASS(McpServer);
		GDREGISTER_INTERNAL_CLASS(McpCapture);
		const bool tool_run = Engine::get_singleton()->is_editor_hint() || Engine::get_singleton()->is_project_manager_hint();
		if (!tool_run) {
			if (OS::get_singleton()->get_cmdline_args().find("--mcp")) {
				McpStartup::take_over_if_asked();
			} else {
				// A game: for screenshots and measurements, and to answer the editor when run from it.
				McpCapture::start_if_asked();
				McpGame::start_if_debugged();
			}
		}
	}
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_INTERNAL_CLASS(McpDebuggerPlugin);
		GDREGISTER_INTERNAL_CLASS(McpEditorPlugin);
		EditorPlugins::add_by_type<McpEditorPlugin>();
	}
}

void uninitialize_mcp_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		McpGame::stop();
		McpTools::cleanup();
		McpDocs::cleanup();
		McpStartup::release();
	}
}
