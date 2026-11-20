#pragma once

// McpServer: Purr's Model Context Protocol server, for AI assistants (`purr --path <project> --mcp`).
// It reads JSON-RPC messages from stdin, one a line, and answers on stdout: both the stateless
// protocol (2026-07-28 on, with server/discover) and the earlier one, which starts with
// initialize. Tools that wait on another process run on threads of their own; the rest run on
// the main thread, between frames. It quits when stdin closes.

#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/templates/list.h"
#include "core/templates/safe_refcount.h"
#include "scene/main/node.h"

struct McpTool;

class McpServer : public Node {
	GDCLASS(McpServer, Node);

	struct Job {
		Thread thread;
		SafeFlag done;
		Variant id;
		const McpTool *tool = nullptr;
		Dictionary arguments;
		bool modern = false;
	};

	Thread reader;
	SafeFlag closed;
	Mutex queue_mutex;
	List<String> queue;
	LocalVector<Job *> jobs;

	static Mutex output_mutex;

	static void _read(void *p_self);
	static void _run_job(void *p_job);
	void _handle(const String &p_line);
	void _call_tool(const Variant &p_id, const Dictionary &p_params, bool p_modern);
	static Dictionary _tool_result(const struct McpResult &p_result, bool p_modern);
	static Dictionary _capabilities();
	static Dictionary _server_info();
	static String _instructions();

	static void _send(const Dictionary &p_message);
	static void _respond(const Variant &p_id, Dictionary p_result, bool p_modern);
	static void _fail(const Variant &p_id, int p_code, const String &p_message, const Variant &p_data = Variant());

protected:
	void _notification(int p_what);

public:
	// Protocol versions: the stateless ones, and those that start with initialize (newest first).
	static const char *MODERN_VERSIONS[];
	static const char *LEGACY_VERSIONS[];

	McpServer();
	~McpServer();
};
