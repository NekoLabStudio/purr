#pragma once

// The Purr editor's end of the MCP bridge: it listens on a local port (written, with a token, to
// .godot/purr_mcp.json) for the MCP server an assistant started on the same project, answers
// what's about the editor itself (open scenes, the edited scene's tree, its log, playing and
// stopping, rescanning files), and passes what's about the running game to it through the
// debugger.

#include "editor/debugger/editor_debugger_plugin.h"
#include "editor/plugins/editor_plugin.h"

#include "../mcp_bridge.h"

class TCPServer;
class McpEditorPlugin;

class McpDebuggerPlugin : public EditorDebuggerPlugin {
	GDCLASS(McpDebuggerPlugin, EditorDebuggerPlugin);

public:
	McpEditorPlugin *owner = nullptr;

	virtual bool has_capture(const String &p_capture) const override;
	virtual bool capture(const String &p_message, const Array &p_data, int p_session) override;
	// The session of the game running now, if there's one.
	Ref<EditorDebuggerSession> active_session();
};

class McpEditorPlugin : public EditorPlugin {
	GDCLASS(McpEditorPlugin, EditorPlugin);

	struct Pending {
		McpBridge::Line *connection = nullptr;
		Variant id;
	};

	Ref<TCPServer> server;
	String token;
	LocalVector<McpBridge::Line *> connections;
	HashMap<int64_t, Pending> pending; // Asked of the game, by key.
	int64_t next_key = 1;
	Ref<McpDebuggerPlugin> debugger;

	static void _run_headless(int p_index, List<String> &r_arguments);
	void _listen();
	void _stop();
	void _handle(McpBridge::Line *p_connection, const Dictionary &p_request);
	Dictionary _editor(const String &p_method, const Dictionary &p_params);

protected:
	void _notification(int p_what);

public:
	void game_replied(const Array &p_data);

	virtual String get_plugin_name() const override { return "MCP"; }

	McpEditorPlugin();
	~McpEditorPlugin();
};
