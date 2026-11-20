#include "mcp_log_buffer.h"

#include "core/os/os.h"
#include <cstdio>

McpLogBuffer *McpLogBuffer::singleton = nullptr;

McpLogBuffer::McpLogBuffer() {
	singleton = this;
}

McpLogBuffer::~McpLogBuffer() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

void McpLogBuffer::install() {
	if (singleton == nullptr) {
		// The OS owns its loggers and frees them as it shuts down.
		OS::get_singleton()->add_logger(memnew(McpLogBuffer));
	}
}

void McpLogBuffer::_add(const String &p_line) {
	MutexLock lock(mutex);
	lines.push_back(p_line);
	while (lines.size() > KEPT) {
		lines.pop_front();
	}
}

void McpLogBuffer::logv(const char *p_format, va_list p_list, bool p_err) {
	char buffer[4096];
	vsnprintf(buffer, sizeof(buffer), p_format, p_list);
	const String text = String::utf8(buffer).strip_edges(false, true);
	if (!text.is_empty()) {
		_add(text);
	}
}

void McpLogBuffer::log_error(const char *p_function, const char *p_file, int p_line, const char *p_code, const char *p_rationale, bool p_editor_notify, ErrorType p_type, const Vector<Ref<ScriptBacktrace>> &p_script_backtraces) {
	const String message = (p_rationale != nullptr && p_rationale[0] != '\0') ? String::utf8(p_rationale) : String::utf8(p_code);
	_add(vformat("%s: %s (at %s:%d)", error_type_string(p_type), message, String::utf8(p_file), p_line));
}

String McpLogBuffer::get_lines(int p_count, bool p_problems_only) {
	MutexLock lock(mutex);
	PackedStringArray picked;
	for (List<String>::Element *line = lines.back(); line != nullptr && picked.size() < p_count; line = line->prev()) {
		const String &text = line->get();
		if (!p_problems_only || text.begins_with("ERROR") || text.begins_with("WARNING") || text.begins_with("SCRIPT ERROR") || text.begins_with("SHADER ERROR") || text.begins_with("USER ERROR") || text.begins_with("USER WARNING")) {
			picked.push_back(text);
		}
	}
	picked.reverse();
	return String("\n").join(picked);
}
