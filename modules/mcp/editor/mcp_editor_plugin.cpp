#include "mcp_editor_plugin.h"

#include "../mcp_log_buffer.h"

#include "core/crypto/crypto.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/tcp_server.h"
#include "core/os/os.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/editor_data.h"
#include "editor/run/editor_run.h"
#include "servers/display/display_server.h"

bool McpDebuggerPlugin::has_capture(const String &p_capture) const {
	return p_capture == "purr_mcp";
}

bool McpDebuggerPlugin::capture(const String &p_message, const Array &p_data, int p_session) {
	if (p_message == "purr_mcp:reply" && owner != nullptr) {
		owner->game_replied(p_data);
		return true;
	}
	return false;
}

Ref<EditorDebuggerSession> McpDebuggerPlugin::active_session() {
	const Array sessions = get_sessions();
	for (int i = 0; i < sessions.size(); i++) {
		Ref<EditorDebuggerSession> session = sessions[i];
		if (session.is_valid() && session->is_active()) {
			return session;
		}
	}
	return Ref<EditorDebuggerSession>();
}

// A headless editor (an assistant's, or CI's) has no screen for the game either.
void McpEditorPlugin::_run_headless(int p_index, List<String> &r_arguments) {
	r_arguments.push_back("--headless");
}

McpEditorPlugin::McpEditorPlugin() {
	McpLogBuffer::install();
	debugger.instantiate();
	debugger->owner = this;
}

McpEditorPlugin::~McpEditorPlugin() {
	_stop();
}

void McpEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			add_debugger_plugin(debugger);
			if (DisplayServer::get_singleton()->get_name() == "headless" && EditorRun::instance_starting_callback == nullptr) {
				EditorRun::instance_starting_callback = &McpEditorPlugin::_run_headless;
			}
			_listen();
			set_process(true);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			remove_debugger_plugin(debugger);
			if (EditorRun::instance_starting_callback == &McpEditorPlugin::_run_headless) {
				EditorRun::instance_starting_callback = nullptr;
			}
			_stop();
		} break;
		case NOTIFICATION_PROCESS: {
			if (server.is_null()) {
				return;
			}
			while (server->is_connection_available()) {
				connections.push_back(memnew(McpBridge::Line(server->take_connection())));
			}
			for (uint32_t i = 0; i < connections.size();) {
				McpBridge::Line *connection = connections[i];
				Dictionary request;
				while (connection->receive(request)) {
					_handle(connection, request);
				}
				if (!connection->is_open()) {
					for (KeyValue<int64_t, Pending> &waiting : pending) {
						if (waiting.value.connection == connection) {
							waiting.value.connection = nullptr;
						}
					}
					memdelete(connection);
					connections.remove_at(i);
				} else {
					i++;
				}
			}
		} break;
	}
}

// On a port of the system's choosing, on this computer only, with a token so only a process that
// can read the project's .godot folder talks to it.
void McpEditorPlugin::_listen() {
	server.instantiate();
	if (server->listen(0, IPAddress("127.0.0.1")) != OK) {
		server.unref();
		return;
	}
	const Ref<Crypto> crypto = Crypto::create();
	const PackedByteArray random = crypto.is_valid() ? crypto->generate_random_bytes(16) : PackedByteArray();
	token = String::hex_encode_buffer(random.ptr(), random.size());
	if (token.is_empty()) {
		token = itos(OS::get_singleton()->get_ticks_usec()) + itos(OS::get_singleton()->get_process_id());
	}
	Dictionary file;
	file["port"] = server->get_local_port();
	file["token"] = token;
	file["pid"] = OS::get_singleton()->get_process_id();
	DirAccess::make_dir_recursive_absolute(McpBridge::file_path().get_base_dir());
	Ref<FileAccess> out = FileAccess::open(McpBridge::file_path(), FileAccess::WRITE);
	if (out.is_valid()) {
		out->store_string(JSON::stringify(file));
	}
}

void McpEditorPlugin::_stop() {
	for (McpBridge::Line *connection : connections) {
		memdelete(connection);
	}
	connections.clear();
	pending.clear();
	if (server.is_valid()) {
		server->stop();
		server.unref();
		// Only this editor's file: another editor may have the project open since.
		const Variant written = JSON::parse_string(FileAccess::get_file_as_string(McpBridge::file_path()));
		if (written.get_type() == Variant::DICTIONARY && String(Dictionary(written).get("token", "")) == token) {
			DirAccess::remove_absolute(McpBridge::file_path());
		}
	}
}

void McpEditorPlugin::_handle(McpBridge::Line *p_connection, const Dictionary &p_request) {
	const Variant id = p_request.get("id", Variant());
	Dictionary reply;
	reply["id"] = id;
	if (String(p_request.get("token", "")) != token) {
		reply["error"] = "Wrong token.";
		p_connection->send(reply);
		return;
	}
	const String method = p_request.get("method", "");
	const Dictionary params = p_request.get("params", Dictionary());
	if (method.begins_with("game_")) {
		const Ref<EditorDebuggerSession> session = debugger->active_session();
		if (session.is_null()) {
			reply["error"] = "The game isn't running from the editor (start it with editor_play).";
			p_connection->send(reply);
			return;
		}
		const int64_t key = next_key++;
		pending[key] = { p_connection, id };
		session->send_message("purr_mcp:" + method.substr(5), Array({ key, params }));
		return;
	}
	reply["result"] = _editor(method, params);
	p_connection->send(reply);
}

void McpEditorPlugin::game_replied(const Array &p_data) {
	if (p_data.size() < 2) {
		return;
	}
	const int64_t key = p_data[0];
	const Pending *waiting = pending.getptr(key);
	if (waiting == nullptr) {
		return;
	}
	if (waiting->connection != nullptr) {
		Dictionary reply;
		reply["id"] = waiting->id;
		reply["result"] = p_data[1];
		waiting->connection->send(reply);
	}
	pending.erase(key);
}

namespace {
String node_line(Node *p_node, Node *p_root, int p_depth) {
	String line = String("  ").repeat(p_depth) + p_node->get_name() + ": " + p_node->get_class();
	if (!p_node->get_scene_file_path().is_empty() && p_node != p_root) {
		line += " (instance of " + p_node->get_scene_file_path() + ")";
	}
	const Ref<Resource> script = p_node->get_script();
	if (script.is_valid() && !script->get_path().is_empty()) {
		line += " (script " + script->get_path() + ")";
	}
	return line + "\n";
}

void describe(Node *p_node, Node *p_root, int p_depth, int p_max_depth, String &r_text) {
	r_text += node_line(p_node, p_root, p_depth);
	if (p_depth >= p_max_depth) {
		return;
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		Node *child = p_node->get_child(i);
		// What the scene owns, not what instanced scenes bring with them.
		if (child->get_owner() == p_root || p_node == p_root || child->get_owner() == p_node) {
			describe(child, p_root, p_depth + 1, p_max_depth, r_text);
		}
	}
}
} // namespace

Dictionary McpEditorPlugin::_editor(const String &p_method, const Dictionary &p_params) {
	EditorInterface *editor = EditorInterface::get_singleton();
	Dictionary result;
	if (p_method == "editor_state") {
		Node *edited = editor->get_edited_scene_root();
		String text = "Open scenes: " + String(", ").join(editor->get_open_scenes()) + "\n";
		text += "Edited: " + (edited != nullptr ? edited->get_scene_file_path() : String("(none)")) + "\n";
		PackedStringArray selected;
		for (Node *node : editor->get_selection()->get_top_selected_node_list()) {
			selected.push_back(edited != nullptr ? String(edited->get_path_to(node)) : String(node->get_name()));
		}
		text += "Selected: " + (selected.is_empty() ? String("(nothing)") : String(", ").join(selected)) + "\n";
		text += "Game: " + (editor->is_playing_scene() ? "running " + editor->get_playing_scene() : String("not running")) + "\n";
		result["text"] = text;
	} else if (p_method == "editor_scene_tree") {
		Node *edited = editor->get_edited_scene_root();
		if (edited == nullptr) {
			result["error"] = "No scene is open in the editor.";
		} else {
			String text = edited->get_scene_file_path() + "\n";
			describe(edited, edited, 0, CLAMP(int(p_params.get("depth", 6)), 0, 50), text);
			result["text"] = text;
		}
	} else if (p_method == "editor_log") {
		result["text"] = McpLogBuffer::singleton != nullptr ? McpLogBuffer::singleton->get_lines(CLAMP(int(p_params.get("lines", 100)), 1, 1000), bool(p_params.get("problems_only", false))) : String();
	} else if (p_method == "editor_play") {
		const String scene = p_params.get("scene", "");
		if (scene == "current") {
			editor->play_current_scene();
		} else if (!scene.is_empty()) {
			editor->play_custom_scene(scene);
		} else {
			editor->play_main_scene();
		}
		result["text"] = "Started. game_ tools reach it once it's up, in a second or two.";
	} else if (p_method == "editor_stop") {
		editor->stop_playing_scene();
		result["text"] = "Stopped.";
	} else if (p_method == "editor_rescan") {
		// Files changed outside the editor: found, imported, and open scenes read again.
		editor->get_resource_filesystem()->scan();
		const String scene = p_params.get("reload_scene", "");
		if (!scene.is_empty()) {
			editor->reload_scene_from_path(scene);
		}
		result["text"] = scene.is_empty() ? String("Rescanning the project's files.") : "Rescanning, and reloading " + scene + ".";
	} else if (p_method == "editor_save") {
		editor->save_all_scenes();
		result["text"] = "Saved the open scenes.";
	} else {
		result["error"] = "Unknown request: " + p_method;
	}
	return result;
}
