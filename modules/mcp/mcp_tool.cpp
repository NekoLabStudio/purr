#include "mcp_tool.h"

#include "core/variant/array.h"

Dictionary McpTool::schema(std::initializer_list<McpParam> p_params) {
	Dictionary properties;
	Array required;
	for (const McpParam &param : p_params) {
		Dictionary property;
		property["type"] = param.type;
		property["description"] = param.description;
		properties[param.name] = property;
		if (param.required) {
			required.push_back(param.name);
		}
	}
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = properties;
	if (!required.is_empty()) {
		schema["required"] = required;
	}
	schema["additionalProperties"] = false;
	return schema;
}

namespace McpTools {

static Vector<McpTool> *tools = nullptr; // Variants can't outlive the engine: never a static.

const Vector<McpTool> &all() {
	if (tools == nullptr) {
		tools = memnew(Vector<McpTool>);
		add_docs_tools(*tools);
		add_project_tools(*tools);
		add_run_tools(*tools);
		add_live_tools(*tools);
	}
	return *tools;
}

void cleanup() {
	if (tools != nullptr) {
		memdelete(tools);
		tools = nullptr;
	}
}

String text(const Dictionary &p_arguments, const char *p_name, const String &p_default) {
	const Variant value = p_arguments.get(p_name, Variant());
	return value.get_type() == Variant::STRING ? String(value) : p_default;
}

int64_t integer(const Dictionary &p_arguments, const char *p_name, int64_t p_default) {
	const Variant value = p_arguments.get(p_name, Variant());
	return value.get_type() == Variant::INT || value.get_type() == Variant::FLOAT ? int64_t(value) : p_default;
}

bool flag(const Dictionary &p_arguments, const char *p_name, bool p_default) {
	const Variant value = p_arguments.get(p_name, Variant());
	return value.get_type() == Variant::BOOL ? bool(value) : p_default;
}

} // namespace McpTools
