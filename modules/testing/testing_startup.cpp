#include "testing_startup.h"

#include "project_check.h"
#include "test_runner.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/packed_scene.h"

namespace {
const char *MAIN_SCENE = "application/run/main_scene";
const char *STAND_IN_PATH = "res://.purr_testing.tscn"; // Never on disk.
Ref<PackedScene> stand_in;
String main_scene;
} // namespace

// Adds the runner (or the check) once the tree is up, on the first frame.
void TestingStartup::_start(bool p_check) {
	SceneTree *tree = SceneTree::get_singleton();
	ERR_FAIL_NULL(tree);
	Node *runner = p_check ? static_cast<Node *>(memnew(ProjectCheck)) : static_cast<Node *>(memnew(TestRunner));
	tree->get_root()->add_child(runner);
}

void TestingStartup::take_over_if_asked() {
	main_scene = GLOBAL_GET(MAIN_SCENE);
#ifndef TOOLS_ENABLED
	// Exported games run as games, whatever they're started with.
	return;
#endif
	if (Engine::get_singleton()->is_editor_hint() || Engine::get_singleton()->is_project_manager_hint()) {
		return;
	}
	bool tests = false;
	bool check = false;
	for (const String &argument : OS::get_singleton()->get_cmdline_args()) {
		tests = tests || argument == "--run-tests" || argument.begins_with("--run-tests=");
		check = check || argument == "--check-project";
	}
	if (!tests && !check) {
		return;
	}
	// An empty scene in place of the game's, so the game doesn't start.
	Node *empty = memnew(Node);
	empty->set_name(check ? "ProjectCheck" : "Tests");
	stand_in.instantiate();
	stand_in->pack(empty);
	memdelete(empty);
	stand_in->set_path(STAND_IN_PATH, true);
	ProjectSettings::get_singleton()->set_setting(MAIN_SCENE, STAND_IN_PATH);
	callable_mp_static(&TestingStartup::_start).call_deferred(check);
}

void TestingStartup::release() {
	stand_in.unref();
}

String TestingStartup::get_main_scene() {
	return main_scene;
}
