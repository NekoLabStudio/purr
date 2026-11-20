#include "mcp_server.h"

#include "mcp_docs.h"
#include "mcp_tool.h"

#include "core/config/engine.h"
#include "core/version.h"
#include "core/io/json.h"
#include "core/object/class_db.h"
#include "scene/main/scene_tree.h"

#include <cstdio>

Mutex McpServer::output_mutex;

const char *McpServer::MODERN_VERSIONS[] = { "2026-07-28", nullptr };
const char *McpServer::LEGACY_VERSIONS[] = { "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05", nullptr };

namespace {
const char *META_VERSION = "io.modelcontextprotocol/protocolVersion";
const char *META_SERVER_INFO = "io.modelcontextprotocol/serverInfo";
constexpr int PARSE_ERROR = -32700;
constexpr int INVALID_REQUEST = -32600;
constexpr int METHOD_NOT_FOUND = -32601;
constexpr int INVALID_PARAMS = -32602;
constexpr int UNSUPPORTED_VERSION = -32022;
constexpr int64_t LIST_TTL_MS = 3600000; // Tools and guides don't change while it runs.

bool listed(const char **p_list, const String &p_version) {
	for (const char **version = p_list; *version != nullptr; version++) {
		if (p_version == *version) {
			return true;
		}
	}
	return false;
}

Array as_array(const char **p_list) {
	Array result;
	for (const char **version = p_list; *version != nullptr; version++) {
		result.push_back(*version);
	}
	return result;
}
} // namespace

McpServer::McpServer() {
	set_name("McpServer");
	set_process_mode(PROCESS_MODE_ALWAYS);
}

McpServer::~McpServer() {
	for (Job *job : jobs) {
		job->thread.wait_to_finish();
		memdelete(job);
	}
	// The reader is left blocked on stdin if it's still open: the process is ending.
	if (reader.is_started() && closed.is_set()) {
		reader.wait_to_finish();
	}
}

void McpServer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			reader.start(&McpServer::_read, this);
			set_process_internal(true);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			List<String> lines;
			{
				MutexLock lock(queue_mutex);
				lines = queue;
				queue.clear();
			}
			for (const String &line : lines) {
				_handle(line);
			}
			for (uint32_t i = 0; i < jobs.size();) {
				if (jobs[i]->done.is_set()) {
					jobs[i]->thread.wait_to_finish();
					memdelete(jobs[i]);
					jobs.remove_at(i);
				} else {
					i++;
				}
			}
			// The assistant has gone: once the last answer's out, so is the server.
			if (closed.is_set() && jobs.is_empty() && lines.is_empty()) {
				get_tree()->quit();
			}
		} break;
	}
}

// Lines from stdin, whole, for the main thread.
void McpServer::_read(void *p_self) {
	McpServer *self = static_cast<McpServer *>(p_self);
	LocalVector<char> line;
	char chunk[65536];
	while (fgets(chunk, sizeof(chunk), stdin) != nullptr) {
		for (const char *c = chunk; *c != '\0'; c++) {
			line.push_back(*c);
		}
		if (line.is_empty() || line[line.size() - 1] != '\n') {
			continue; // A longer line: more to come.
		}
		const String text = String::utf8(line.ptr(), line.size()).strip_edges();
		line.clear();
		if (!text.is_empty()) {
			MutexLock lock(self->queue_mutex);
			self->queue.push_back(text);
		}
	}
	self->closed.set();
}

void McpServer::_send(const Dictionary &p_message) {
	// One line each: JSON escapes the newlines in strings.
	const CharString text = JSON::stringify(p_message, "", false).utf8();
	MutexLock lock(output_mutex);
	fwrite(text.get_data(), 1, text.length(), stdout);
	fputc('\n', stdout);
	fflush(stdout);
}

void McpServer::_respond(const Variant &p_id, Dictionary p_result, bool p_modern) {
	if (p_modern && !p_result.has("resultType")) {
		p_result["resultType"] = "complete";
	}
	Dictionary message;
	message["jsonrpc"] = "2.0";
	message["id"] = p_id;
	message["result"] = p_result;
	_send(message);
}

void McpServer::_fail(const Variant &p_id, int p_code, const String &p_message, const Variant &p_data) {
	Dictionary error;
	error["code"] = p_code;
	error["message"] = p_message;
	if (p_data.get_type() != Variant::NIL) {
		error["data"] = p_data;
	}
	Dictionary message;
	message["jsonrpc"] = "2.0";
	message["id"] = p_id;
	message["error"] = error;
	_send(message);
}

Dictionary McpServer::_capabilities() {
	Dictionary capabilities;
	capabilities["tools"] = Dictionary();
	capabilities["resources"] = Dictionary();
	return capabilities;
}

Dictionary McpServer::_server_info() {
	Dictionary info;
	info["name"] = "purr";
	info["title"] = "Purr";
	info["version"] = String(GODOT_VERSION_BUILD);
	return info;
}

String McpServer::_instructions() {
	return "This is a Purr project. Purr is Godot with what games need built in: accounts, saves, achievements, "
		   "leaderboards, lobbies, Steam, Game Center, Play Games and iCloud; controls and steering wheels; VR; virtual "
		   "cameras and photo mode; terrain, scattering, water, weather, time of day and large worlds; screen effects and "
		   "graphics settings; debug drawing, a console, tests and behaviour trees. Before writing something a game "
		   "commonly needs, use search_docs: Purr may already do it, configured in Project Settings rather than code. "
		   "read_doc gives a guide or a class's reference (Godot's classes too). Check work with check_project and "
		   "run_tests, run_scene for errors and screenshot to see it. The editor_ and game_ tools reach the Purr editor "
		   "open on this project, and the game it's running.";
}

void McpServer::_handle(const String &p_line) {
	JSON json;
	if (json.parse(p_line) != OK) {
		_fail(Variant(), PARSE_ERROR, "Parse error: " + json.get_error_message());
		return;
	}
	const Variant parsed = json.get_data();
	if (parsed.get_type() != Variant::DICTIONARY) {
		_fail(Variant(), INVALID_REQUEST, "One JSON-RPC message a line.");
		return;
	}
	const Dictionary message = parsed;
	if (!message.has("method")) {
		return; // A response to something the server didn't ask: nothing to do.
	}
	const String method = message["method"];
	const bool notification = !message.has("id");
	Variant id = message.get("id", Variant());
	// JSON numbers parse as floats, but an id is a string or an integer, and goes back as it came.
	if (id.get_type() == Variant::FLOAT && double(id) == Math::floor(double(id))) {
		id = int64_t(double(id));
	}
	const Dictionary params = message.get("params", Dictionary());
	if (notification) {
		return; // notifications/initialized, cancelled and the like: nothing to answer.
	}

	// The stateless protocol names its version in every request.
	const Dictionary meta = params.get("_meta", Dictionary());
	const bool modern = meta.has(META_VERSION);
	if (modern && !listed(MODERN_VERSIONS, meta[META_VERSION])) {
		Dictionary data;
		data["supported"] = as_array(MODERN_VERSIONS);
		data["requested"] = meta[META_VERSION];
		_fail(id, UNSUPPORTED_VERSION, "Unsupported protocol version", data);
		return;
	}

	if (method == "initialize") {
		const String asked = params.get("protocolVersion", "");
		Dictionary result;
		result["protocolVersion"] = listed(LEGACY_VERSIONS, asked) ? asked : String(LEGACY_VERSIONS[0]);
		result["capabilities"] = _capabilities();
		result["serverInfo"] = _server_info();
		result["instructions"] = _instructions();
		_respond(id, result, false);
	} else if (method == "server/discover") {
		Array supported = as_array(MODERN_VERSIONS);
		supported.append_array(as_array(LEGACY_VERSIONS));
		Dictionary result;
		result["supportedVersions"] = supported;
		result["capabilities"] = _capabilities();
		result["instructions"] = _instructions();
		result["ttlMs"] = LIST_TTL_MS;
		result["cacheScope"] = "public";
		Dictionary result_meta;
		result_meta[META_SERVER_INFO] = _server_info();
		result["_meta"] = result_meta;
		_respond(id, result, true);
	} else if (method == "ping") {
		_respond(id, Dictionary(), modern);
	} else if (method == "tools/list") {
		Array tools;
		for (const McpTool &tool : McpTools::all()) {
			Dictionary entry;
			entry["name"] = tool.name;
			entry["title"] = tool.title;
			entry["description"] = tool.description;
			entry["inputSchema"] = tool.input_schema;
			Dictionary annotations;
			annotations["readOnlyHint"] = tool.read_only;
			annotations["openWorldHint"] = false;
			entry["annotations"] = annotations;
			tools.push_back(entry);
		}
		Dictionary result;
		result["tools"] = tools;
		if (modern) {
			result["ttlMs"] = LIST_TTL_MS;
			result["cacheScope"] = "public";
		}
		_respond(id, result, modern);
	} else if (method == "tools/call") {
		_call_tool(id, params, modern);
	} else if (method == "resources/list") {
		Dictionary result;
		result["resources"] = McpDocs::guide_resources();
		if (modern) {
			result["ttlMs"] = LIST_TTL_MS;
			result["cacheScope"] = "public";
		}
		_respond(id, result, modern);
	} else if (method == "resources/templates/list") {
		Dictionary result;
		result["resourceTemplates"] = Array();
		_respond(id, result, modern);
	} else if (method == "resources/read") {
		const String uri = params.get("uri", "");
		const Dictionary contents = McpDocs::read_guide_resource(uri);
		if (contents.is_empty()) {
			_fail(id, INVALID_PARAMS, "No such resource: " + uri);
			return;
		}
		Dictionary result;
		result["contents"] = Array({ contents });
		if (modern) {
			result["ttlMs"] = LIST_TTL_MS;
			result["cacheScope"] = "public";
		}
		_respond(id, result, modern);
	} else {
		_fail(id, METHOD_NOT_FOUND, "Method not found: " + method);
	}
}

Dictionary McpServer::_tool_result(const McpResult &p_result, bool p_modern) {
	Array content;
	Dictionary text;
	text["type"] = "text";
	text["text"] = p_result.text;
	content.push_back(text);
	for (const Dictionary &image : p_result.images) {
		Dictionary entry = image.duplicate();
		entry["type"] = "image";
		content.push_back(entry);
	}
	Dictionary result;
	result["content"] = content;
	if (p_result.structured.get_type() == Variant::DICTIONARY) {
		result["structuredContent"] = p_result.structured;
	}
	result["isError"] = p_result.error;
	return result;
}

void McpServer::_run_job(void *p_job) {
	Job *job = static_cast<Job *>(p_job);
	const McpResult result = job->tool->run(job->arguments);
	_respond(job->id, _tool_result(result, job->modern), job->modern);
	job->done.set();
}

void McpServer::_call_tool(const Variant &p_id, const Dictionary &p_params, bool p_modern) {
	const String name = p_params.get("name", "");
	const McpTool *tool = nullptr;
	for (const McpTool &each : McpTools::all()) {
		if (each.name == name) {
			tool = &each;
			break;
		}
	}
	if (tool == nullptr) {
		_fail(p_id, INVALID_PARAMS, "Unknown tool: " + name);
		return;
	}
	const Variant arguments = p_params.get("arguments", Dictionary());
	if (arguments.get_type() != Variant::DICTIONARY) {
		_fail(p_id, INVALID_PARAMS, "A tool's arguments are an object.");
		return;
	}
	if (!tool->slow) {
		_respond(p_id, _tool_result(tool->run(arguments), p_modern), p_modern);
		return;
	}
	Job *job = memnew(Job);
	job->id = p_id;
	job->tool = tool;
	job->arguments = arguments;
	job->modern = p_modern;
	jobs.push_back(job);
	job->thread.start(&McpServer::_run_job, job);
}
