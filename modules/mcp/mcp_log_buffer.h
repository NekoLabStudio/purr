#pragma once

// The last lines a process logged (prints, warnings and errors), for an assistant to read: the
// editor's own, and a game's, through the MCP bridge.

#include "core/io/logger.h"
#include "core/os/mutex.h"
#include "core/templates/list.h"

class McpLogBuffer : public Logger {
	static constexpr int KEPT = 1000;

	Mutex mutex;
	List<String> lines;

	void _add(const String &p_line);

public:
	static McpLogBuffer *singleton;

	virtual void logv(const char *p_format, va_list p_list, bool p_err) override _PRINTF_FORMAT_ATTRIBUTE_2_0;
	virtual void log_error(const char *p_function, const char *p_file, int p_line, const char *p_code, const char *p_rationale, bool p_editor_notify, ErrorType p_type, const Vector<Ref<ScriptBacktrace>> &p_script_backtraces) override;

	// The last p_count lines, oldest first; only errors and warnings when p_problems_only.
	String get_lines(int p_count, bool p_problems_only);

	// Starts keeping this process's log.
	static void install();

	McpLogBuffer();
	~McpLogBuffer();
};
