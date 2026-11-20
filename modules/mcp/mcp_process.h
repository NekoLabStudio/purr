#pragma once

// Runs Purr again on the project, as a child process, for the tools that run the game or its
// tests: its output (stdout and stderr together) and how it ended, killed if it outlasts its time.

#include "core/variant/variant.h"
#include "core/string/ustring.h"
#include "core/templates/list.h"

namespace McpProcess {

struct Run {
	String output;
	int exit_code = -1;
	bool timed_out = false;
	bool started = false;
};

Run run(const List<String> &p_arguments, double p_timeout_seconds);
// The project's path, for a child's --path.
String project_path();
// Errors and warnings in a run's output, each with its "at:" line.
PackedStringArray problems(const String &p_output);
// The last lines of a run's output.
String tail(const String &p_output, int p_lines);

} // namespace McpProcess
