#pragma once

// One tool the MCP server offers an AI assistant: its name and description as the assistant sees
// them, the JSON Schema of its arguments, and what it does. A tool's result is text for the
// assistant to read, with structured data and images beside it when it has them.

#include "core/string/ustring.h"
#include "core/variant/variant.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

#include <initializer_list>

struct McpResult {
	String text;
	Variant structured; // A Dictionary, when there's data to go with the text.
	Vector<Dictionary> images; // { data: base64, mimeType }
	bool error = false;

	static McpResult failed(const String &p_text) {
		McpResult result;
		result.text = p_text;
		result.error = true;
		return result;
	}
};

struct McpParam {
	const char *name;
	const char *type; // "string", "integer", "number", "boolean"
	const char *description;
	bool required = false;
};

struct McpTool {
	String name;
	String title;
	String description;
	Dictionary input_schema;
	bool read_only = true;
	// Run off the main thread: it waits on another process (a test run, the editor).
	bool slow = false;
	McpResult (*run)(const Dictionary &p_arguments) = nullptr;

	static Dictionary schema(std::initializer_list<McpParam> p_params);
};

namespace McpTools {

// Every tool, in the order the assistant sees them. Made when first asked for, freed by cleanup().
const Vector<McpTool> &all();
void cleanup();

// The areas' own lists.
void add_docs_tools(Vector<McpTool> &r_tools);
void add_project_tools(Vector<McpTool> &r_tools);
void add_run_tools(Vector<McpTool> &r_tools);
void add_live_tools(Vector<McpTool> &r_tools);

// An argument, or its default.
String text(const Dictionary &p_arguments, const char *p_name, const String &p_default = String());
int64_t integer(const Dictionary &p_arguments, const char *p_name, int64_t p_default);
bool flag(const Dictionary &p_arguments, const char *p_name, bool p_default);

} // namespace McpTools
