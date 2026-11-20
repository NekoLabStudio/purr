#include "mcp_bridge.h"
#include "mcp_tool.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"

namespace {

constexpr double WAIT_SECONDS = 20.0;

// Asks the editor open on this project; the result's "text" (and data), or why it couldn't.
Dictionary ask(const String &p_method, const Dictionary &p_params) {
	Dictionary failed;
	const Variant file = JSON::parse_string(FileAccess::get_file_as_string(McpBridge::file_path()));
	if (file.get_type() != Variant::DICTIONARY) {
		failed["error"] = "The Purr editor isn't open on this project, so there's no editor or running game to reach. Open the project in the editor.";
		return failed;
	}
	const Dictionary where = file;
	Ref<StreamPeerTCP> peer;
	peer.instantiate();
	if (peer->connect_to_host(IPAddress("127.0.0.1"), int(where.get("port", 0))) != OK) {
		failed["error"] = "Couldn't reach the editor.";
		return failed;
	}
	McpBridge::Line line(peer);
	const uint64_t started = OS::get_singleton()->get_ticks_msec();
	auto waited = [&]() { return double(OS::get_singleton()->get_ticks_msec() - started) / 1000.0; };
	while (peer->poll() == OK && peer->get_status() == StreamPeerTCP::STATUS_CONNECTING && waited() < 5.0) {
		OS::get_singleton()->delay_usec(10000);
	}
	if (peer->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
		failed["error"] = "Couldn't reach the editor: it may have closed without saying so. Open the project in the editor again.";
		return failed;
	}
	Dictionary request;
	request["token"] = where.get("token", "");
	request["id"] = 1;
	request["method"] = p_method;
	request["params"] = p_params;
	line.send(request);
	Dictionary reply;
	while (!line.receive(reply)) {
		if (!line.is_open() || waited() > WAIT_SECONDS) {
			failed["error"] = p_method.begins_with("game_") ? "The game didn't answer: it may be paused at a breakpoint, or have closed." : "The editor didn't answer.";
			return failed;
		}
		OS::get_singleton()->delay_usec(10000);
	}
	if (reply.has("error")) {
		failed["error"] = reply["error"];
		return failed;
	}
	return reply.get("result", Dictionary());
}

McpResult as_result(const Dictionary &p_answer) {
	if (p_answer.has("error")) {
		return McpResult::failed(p_answer["error"]);
	}
	McpResult result;
	result.text = p_answer.get("text", "");
	if (p_answer.has("png")) {
		Dictionary image;
		image["data"] = p_answer["png"];
		image["mimeType"] = "image/png";
		result.images.push_back(image);
	}
	if (p_answer.has("monitors")) {
		Dictionary structured;
		structured["monitors"] = p_answer["monitors"];
		result.structured = structured;
	}
	return result;
}

#define LIVE_TOOL(m_function, m_method)                       \
	McpResult m_function(const Dictionary &p_arguments) {     \
		return as_result(ask(m_method, p_arguments));         \
	}

LIVE_TOOL(editor_state, "editor_state")
LIVE_TOOL(editor_scene_tree, "editor_scene_tree")
LIVE_TOOL(editor_log, "editor_log")
LIVE_TOOL(editor_play, "editor_play")
LIVE_TOOL(editor_stop, "editor_stop")
LIVE_TOOL(editor_rescan, "editor_rescan")
LIVE_TOOL(editor_save, "editor_save")
LIVE_TOOL(game_tree, "game_tree")
LIVE_TOOL(game_log, "game_log")
LIVE_TOOL(game_monitors, "game_monitors")
LIVE_TOOL(game_console, "game_console")
LIVE_TOOL(game_eval, "game_eval")
LIVE_TOOL(game_screenshot, "game_screenshot")

#undef LIVE_TOOL

McpTool live(const char *p_name, const char *p_title, const char *p_description, const Dictionary &p_schema, McpResult (*p_run)(const Dictionary &), bool p_read_only = true) {
	McpTool tool;
	tool.name = p_name;
	tool.title = p_title;
	tool.description = p_description;
	tool.input_schema = p_schema;
	tool.slow = true;
	tool.read_only = p_read_only;
	tool.run = p_run;
	return tool;
}

} // namespace

void McpTools::add_live_tools(Vector<McpTool> &r_tools) {
	const McpParam lines = { "lines", "integer", "How many of the last lines, 100 if not given." };
	const McpParam problems_only = { "problems_only", "boolean", "Only errors and warnings." };

	r_tools.push_back(live("editor_state", "Editor state",
			"What the Purr editor open on this project is doing: its open scenes, the scene being edited, what's selected, and whether the game is running.",
			McpTool::schema({}), &editor_state));
	r_tools.push_back(live("editor_scene_tree", "Edited scene's tree",
			"The tree of the scene being edited in the editor, unsaved changes and all: each node's name, type, script and the scenes it instances.",
			McpTool::schema({ { "depth", "integer", "How deep, 6 if not given." } }), &editor_scene_tree));
	r_tools.push_back(live("editor_log", "Editor log", "The last lines of the editor's own log: import problems, script errors it found, warnings.",
			McpTool::schema({ lines, problems_only }), &editor_log));
	r_tools.push_back(live("editor_play", "Play the game",
			"Runs the game from the editor, as its play button does, so the game_ tools can reach it. Stop it with editor_stop.",
			McpTool::schema({ { "scene", "string", "A scene to run instead of the main one (res://...), or \"current\" for the one being edited." } }), &editor_play, false));
	r_tools.push_back(live("editor_stop", "Stop the game", "Stops the game the editor is running.", McpTool::schema({}), &editor_stop, false));
	r_tools.push_back(live("editor_rescan", "Rescan files",
			"Has the editor pick up files changed outside it (as an assistant's edits are): new and changed files are found and imported. "
			"With reload_scene, an open scene whose file you changed is read again, so the editor doesn't save its old copy over yours.",
			McpTool::schema({ { "reload_scene", "string", "An open scene to read again from its file, such as res://main.tscn." } }), &editor_rescan, false));
	r_tools.push_back(live("editor_save", "Save scenes", "Saves the scenes open in the editor, before changing their files from outside it.", McpTool::schema({}), &editor_save, false));

	r_tools.push_back(live("game_tree", "Game's scene tree",
			"The running game's live scene tree: each node's name, type, script, and where 3D nodes are, or that a node is hidden.",
			McpTool::schema({
					{ "path", "string", "The node to start from, /root if not given." },
					{ "depth", "integer", "How deep, 4 if not given." },
			}),
			&game_tree));
	r_tools.push_back(live("game_log", "Game's log", "The last lines of the running game's log: its prints, warnings and errors.", McpTool::schema({ lines, problems_only }), &game_log));
	r_tools.push_back(live("game_monitors", "Game's monitors",
			"The running game's performance monitors: frame time, physics, objects, draw calls, memory, and Purr's own (graphics/gpu_ms).",
			McpTool::schema({}), &game_monitors));
	r_tools.push_back(live("game_console", "Game console",
			"Runs a command in the running game's console (Purr's Console: the game's own commands and the built-in ones; \"help\" lists them) and returns what it printed.",
			McpTool::schema({ { "command", "string", "The command line, as typed in the console.", true } }), &game_console, false));
	r_tools.push_back(live("game_eval", "Evaluate in the game",
			"Evaluates a Godot Expression in the running game, on a node: read state (\"get_node('Player').global_position\") or change it. Expressions can call methods, so they can do anything the game can.",
			McpTool::schema({
					{ "expression", "string", "The expression, in Godot's Expression syntax.", true },
					{ "node", "string", "The node it runs on (its self), /root if not given." },
			}),
			&game_eval, false));
	r_tools.push_back(live("game_screenshot", "Game screenshot", "A picture of the running game's window as it is now.", McpTool::schema({}), &game_screenshot));
}
