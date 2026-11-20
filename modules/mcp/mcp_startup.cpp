#include "mcp_startup.h"

#include "mcp_server.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/packed_scene.h"

namespace McpStartup {

namespace {
const char *MAIN_SCENE = "application/run/main_scene";
const char *STAND_IN_PATH = "res://.purr_mcp.tscn"; // Never on disk.
Ref<PackedScene> stand_in;
String main_scene;
Dictionary *autoloads = nullptr;

void start() {
	SceneTree *tree = SceneTree::get_singleton();
	ERR_FAIL_NULL(tree);
	tree->get_root()->add_child(memnew(McpServer));
}
} // namespace

void take_over_if_asked() {
	if (Engine::get_singleton()->is_editor_hint() || Engine::get_singleton()->is_project_manager_hint() || !OS::get_singleton()->get_cmdline_args().find("--mcp")) {
		return;
	}
	main_scene = GLOBAL_GET(MAIN_SCENE);
	autoloads = memnew(Dictionary);
	ProjectSettings *settings = ProjectSettings::get_singleton();
	LocalVector<StringName> names;
	for (const KeyValue<StringName, ProjectSettings::AutoloadInfo> &autoload : settings->get_autoload_list()) {
		(*autoloads)[String(autoload.key)] = autoload.value.path;
		names.push_back(autoload.key);
	}
	for (const StringName &name : names) {
		settings->remove_autoload(name);
	}
	Node *empty = memnew(Node);
	empty->set_name("Mcp");
	stand_in.instantiate();
	stand_in->pack(empty);
	memdelete(empty);
	stand_in->set_path(STAND_IN_PATH, true);
	settings->set_setting(MAIN_SCENE, STAND_IN_PATH);
	callable_mp_static(&start).call_deferred();
}

String get_main_scene() {
	return main_scene;
}

Dictionary get_autoloads() {
	return autoloads != nullptr ? *autoloads : Dictionary();
}

void release() {
	stand_in.unref();
	if (autoloads != nullptr) {
		memdelete(autoloads);
		autoloads = nullptr;
	}
}

} // namespace McpStartup
