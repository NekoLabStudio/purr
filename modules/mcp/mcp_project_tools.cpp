#include "mcp_startup.h"
#include "mcp_tool.h"

#include "core/config/engine.h"
#include "core/version.h"
#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/os/os.h"
#include "scene/resources/packed_scene.h"

namespace {

String value_text(const Variant &p_value) {
	String text = p_value.get_type() == Variant::STRING ? "\"" + String(p_value) + "\"" : p_value.stringify();
	if (text.length() > 200) {
		text = text.substr(0, 200) + "...";
	}
	return text;
}

McpResult project_info(const Dictionary &p_arguments) {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	Dictionary info;
	info["name"] = GLOBAL_GET("application/config/name");
	info["path"] = settings->globalize_path("res://");
	info["main_scene"] = ResourceUID::ensure_path(McpStartup::get_main_scene());
	info["purr_version"] = String(GODOT_VERSION_BUILD);
	info["rendering_method"] = GLOBAL_GET("rendering/renderer/rendering_method");
	info["autoloads"] = McpStartup::get_autoloads();
	// The game's own actions: Godot's ui_ ones only when the game changed them.
	Array actions;
	PackedStringArray action_names;
	List<PropertyInfo> properties;
	settings->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (property.name.begins_with("input/") && (!property.name.begins_with("input/ui_") || settings->get_setting(property.name) != settings->property_get_revert(property.name))) {
			actions.push_back(property.name.substr(6));
			action_names.push_back(property.name.substr(6));
		}
	}
	info["input_actions"] = actions;
	info["tests_folder"] = GLOBAL_GET("testing/tests_folder");

	McpResult result;
	result.text = vformat("%s (Purr %s)\nPath: %s\nMain scene: %s\nRenderer: %s\nAutoloads: %s\nInput actions: %s\nTests: %s",
			info["name"], info["purr_version"], info["path"], info["main_scene"], info["rendering_method"],
			info["autoloads"].stringify(), String(", ").join(action_names), info["tests_folder"]);
	result.structured = info;
	return result;
}

McpResult project_settings(const Dictionary &p_arguments) {
	const String prefix = McpTools::text(p_arguments, "prefix");
	const bool changed_only = McpTools::flag(p_arguments, "changed_only", false);
	ProjectSettings *settings = ProjectSettings::get_singleton();
	List<PropertyInfo> properties;
	settings->get_property_list(&properties);
	Array found;
	McpResult result;
	for (const PropertyInfo &property : properties) {
		if (!(property.usage & PROPERTY_USAGE_STORAGE) || !property.name.begins_with(prefix) || property.name.contains("/.") || property.name.begins_with("autoload/")) {
			continue;
		}
		const Variant value = settings->get_setting(property.name);
		const Variant initial = settings->property_get_revert(property.name);
		const bool changed = value != initial;
		if (changed_only && !changed) {
			continue;
		}
		if (found.size() >= 300) {
			result.text += "(More: narrow the prefix.)\n";
			break;
		}
		Dictionary entry;
		entry["name"] = property.name;
		entry["value"] = value_text(value);
		entry["default"] = value_text(initial);
		entry["changed"] = changed;
		found.push_back(entry);
		result.text += property.name + " = " + value_text(value) + (changed ? "  (default " + value_text(initial) + ")" : String()) + "\n";
	}
	if (found.is_empty()) {
		result.text = "No settings under \"" + prefix + "\".";
	}
	Dictionary structured;
	structured["settings"] = found;
	result.structured = structured;
	return result;
}

McpResult scene_tree(const Dictionary &p_arguments) {
	const String path = McpTools::text(p_arguments, "path");
	const bool with_properties = McpTools::flag(p_arguments, "properties", false);
	Error error = OK;
	Ref<PackedScene> scene = ResourceLoader::load(path, "PackedScene", ResourceFormatLoader::CACHE_MODE_REUSE, &error);
	if (scene.is_null()) {
		return McpResult::failed(vformat("Couldn't load the scene %s (%s).", path, error_names[error]));
	}
	const Ref<SceneState> state = scene->get_state();
	McpResult result;
	Array nodes;
	for (int i = 0; i < state->get_node_count(); i++) {
		const String node_path = String(state->get_node_path(i)).trim_prefix("./");
		String type = state->get_node_type(i);
		const Ref<PackedScene> instance = state->get_node_instance(i);
		if (instance.is_valid()) {
			type = "instance of " + instance->get_path();
		}
		Dictionary node;
		node["path"] = node_path.is_empty() ? "." : node_path;
		node["type"] = type;
		Array groups;
		PackedStringArray group_names;
		for (const StringName &group : state->get_node_groups(i)) {
			groups.push_back(group);
			group_names.push_back(group);
		}
		node["groups"] = groups;
		String line = vformat("%s: %s", node["path"], type);
		Dictionary properties;
		for (int p = 0; p < state->get_node_property_count(i); p++) {
			const String name = state->get_node_property_name(i, p);
			const Variant value = state->get_node_property_value(i, p);
			if (name == "script") {
				const Ref<Resource> script = value;
				node["script"] = script.is_valid() ? script->get_path() : String();
				line += vformat(" (script %s)", node["script"]);
			} else if (with_properties) {
				properties[name] = value_text(value);
			}
		}
		if (with_properties) {
			node["properties"] = properties;
			for (const KeyValue<Variant, Variant> &property : properties) {
				line += vformat("\n    %s = %s", property.key, property.value);
			}
		}
		if (!groups.is_empty()) {
			line += vformat(" [groups: %s]", String(", ").join(group_names));
		}
		result.text += line + "\n";
		nodes.push_back(node);
	}
	Dictionary structured;
	structured["nodes"] = nodes;
	result.structured = structured;
	return result;
}

} // namespace

void McpTools::add_project_tools(Vector<McpTool> &r_tools) {
	McpTool info;
	info.name = "project_info";
	info.title = "Project info";
	info.description = "The project's name, path, Purr version, main scene, renderer, autoloads, input actions and tests folder.";
	info.input_schema = McpTool::schema({});
	info.run = &project_info;
	r_tools.push_back(info);

	McpTool settings;
	settings.name = "project_settings";
	settings.title = "Project settings";
	settings.description = "Lists project settings under a prefix, with their values and defaults: Godot's and Purr's (Purr's live in sections by subject, "
						   "such as saves/, achievements/, online/, input_devices/steering_wheels, rendering/weather/). Purr's features are configured here rather than in code.";
	settings.input_schema = McpTool::schema({
			{ "prefix", "string", "Only settings whose name starts with this, such as \"saves/\" or \"rendering/\". Empty for all." },
			{ "changed_only", "boolean", "Only those the project has changed from their defaults." },
	});
	settings.run = &project_settings;
	r_tools.push_back(settings);

	McpTool tree;
	tree.name = "scene_tree";
	tree.title = "Scene tree";
	tree.description = "A saved scene's nodes, as the file has them: each node's path, type (or the scene it instances), script and groups, and with properties "
					   "the values it sets. For the scene open in the editor, unsaved changes and all, use editor_scene_tree.";
	tree.input_schema = McpTool::schema({
			{ "path", "string", "The scene, such as res://main.tscn.", true },
			{ "properties", "boolean", "Include the properties each node sets." },
	});
	tree.run = &scene_tree;
	r_tools.push_back(tree);
}
