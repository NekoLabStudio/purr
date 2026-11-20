#include "mcp_process.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/variant/dictionary.h"

namespace McpProcess {

String project_path() {
	return ProjectSettings::get_singleton()->globalize_path("res://").trim_suffix("/");
}

Run run(const List<String> &p_arguments, double p_timeout_seconds) {
	Run result;
	OS *os = OS::get_singleton();
	const Dictionary pipes = os->execute_with_pipe(os->get_executable_path(), p_arguments, false);
	if (pipes.is_empty()) {
		result.output = "Couldn't start Purr.";
		return result;
	}
	result.started = true;
	const Ref<FileAccess> out = pipes["stdio"];
	const Ref<FileAccess> err = pipes["stderr"];
	const ProcessID pid = int64_t(pipes["pid"]);
	const uint64_t started = os->get_ticks_msec();
	PackedByteArray collected;
	uint8_t buffer[16384];
	auto drain = [&]() {
		bool any = false;
		for (const Ref<FileAccess> &pipe : { out, err }) {
			uint64_t read = 0;
			while ((read = pipe->get_buffer(buffer, sizeof(buffer))) > 0) {
				const int64_t at = collected.size();
				collected.resize(at + read);
				memcpy(collected.ptrw() + at, buffer, read);
				any = true;
			}
		}
		return any;
	};
	while (os->is_process_running(pid)) {
		if (!drain()) {
			os->delay_usec(20000);
		}
		if (double(os->get_ticks_msec() - started) / 1000.0 > p_timeout_seconds) {
			os->kill(pid);
			result.timed_out = true;
			break;
		}
	}
	drain();
	result.exit_code = result.timed_out ? -1 : os->get_process_exit_code(pid);
	result.output = String::utf8((const char *)collected.ptr(), collected.size());
	return result;
}

PackedStringArray problems(const String &p_output) {
	PackedStringArray found;
	const PackedStringArray lines = p_output.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		const String line = lines[i].strip_edges();
		if (line.begins_with("ERROR:") || line.begins_with("SCRIPT ERROR:") || line.begins_with("SHADER ERROR:") || line.begins_with("USER ERROR:") ||
				line.begins_with("WARNING:") || line.begins_with("USER WARNING:") || line.begins_with("SCRIPT WARNING:")) {
			String problem = line;
			if (i + 1 < lines.size() && lines[i + 1].strip_edges().begins_with("at:")) {
				problem += " (" + lines[i + 1].strip_edges() + ")";
			}
			if (!found.has(problem)) {
				found.push_back(problem);
			}
		}
	}
	return found;
}

String tail(const String &p_output, int p_lines) {
	PackedStringArray lines = p_output.strip_edges().split("\n");
	if (lines.size() > p_lines) {
		lines = lines.slice(lines.size() - p_lines);
	}
	return String("\n").join(lines);
}

} // namespace McpProcess
