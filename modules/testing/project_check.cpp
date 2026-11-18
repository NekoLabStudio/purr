#include "project_check.h"

#include "testing_startup.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/packed_scene.h"

ProjectCheck::ProjectCheck() {
	set_name("ProjectCheck");
	set_process_mode(PROCESS_MODE_ALWAYS);
}

void ProjectCheck::_notification(int p_what) {
	if (p_what != NOTIFICATION_READY) {
		return;
	}
	excluded = GLOBAL_GET("testing/check_excludes");
	_check_settings();
	_check_folder("res://");
	_finish();
}

void ProjectCheck::_error(const String &p_file, const String &p_message) {
	problems.push_back({ p_file, p_message, true });
	print_line(vformat("ERROR    %s: %s", p_file, p_message));
}

void ProjectCheck::_warning(const String &p_file, const String &p_message) {
	problems.push_back({ p_file, p_message, false });
	print_line(vformat("WARNING  %s: %s", p_file, p_message));
}

// The main scene and autoloads, which the game can't start without.
void ProjectCheck::_check_settings() {
	const String main_scene = ResourceUID::ensure_path(TestingStartup::get_main_scene());
	if (main_scene.is_empty()) {
		_warning("project.godot", "there's no main scene, so the game has nothing to run.");
	} else if (!ResourceLoader::exists(main_scene)) {
		_error("project.godot", vformat("the main scene is %s, which isn't there.", main_scene));
	}
	List<PropertyInfo> properties;
	ProjectSettings::get_singleton()->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (!property.name.begins_with("autoload/")) {
			continue;
		}
		String path = GLOBAL_GET(property.name);
		path = ResourceUID::ensure_path(path.trim_prefix("*"));
		if (!ResourceLoader::exists(path)) {
			_error("project.godot", vformat("the autoload %s is %s, which isn't there.", property.name.get_slicec('/', 1), path));
		}
	}
}

void ProjectCheck::_check_folder(const String &p_folder) {
	// An excluded folder and what's in it: res://addons, not res://addons_extra.
	for (const String &prefix : excluded) {
		const String folder = prefix.trim_suffix("/");
		if (!folder.is_empty() && (p_folder == folder || p_folder.begins_with(folder + "/"))) {
			return;
		}
	}
	Ref<DirAccess> dir = DirAccess::open(p_folder);
	// A .gdignore hides a folder from the project, except the project's own root.
	if (dir.is_null() || (p_folder != "res://" && dir->file_exists(".gdignore"))) {
		return;
	}
	Vector<String> files;
	Vector<String> folders;
	dir->list_dir_begin();
	for (String name = dir->get_next(); !name.is_empty(); name = dir->get_next()) {
		if (name.begins_with(".")) {
			continue;
		}
		if (dir->current_is_dir()) {
			folders.push_back(name);
		} else {
			files.push_back(name);
		}
	}
	dir->list_dir_end();
	files.sort();
	folders.sort();
	for (const String &name : files) {
		_check_file(p_folder.path_join(name));
	}
	for (const String &folder : folders) {
		_check_folder(p_folder.path_join(folder));
	}
}

void ProjectCheck::_check_file(const String &p_path) {
	const String extension = p_path.get_extension().to_lower();
	if (extension == "gd") {
		checked++;
		const Ref<Script> script = ResourceLoader::load(p_path, "Script");
		if (script.is_null() || !script->is_script_valid()) {
			_error(p_path, "doesn't compile (see the errors above).");
		}
		return;
	}
	if (extension != "tscn" && extension != "scn" && extension != "tres" && extension != "res") {
		return;
	}
	checked++;
	const int before = problems.size();
	_check_dependencies(p_path);
	for (int i = before; i < problems.size(); i++) {
		if (problems[i].error) {
			return; // Loading it would only repeat the missing files.
		}
	}
	const Ref<Resource> resource = ResourceLoader::load(p_path);
	if (resource.is_null()) {
		_error(p_path, "doesn't load (see the errors above).");
		return;
	}
	_check_scene(p_path, resource);
}

// A dependency is a path, or a UID with the path it was last at. A UID that no longer finds its
// file still loads from that path, so it's only a warning.
bool ProjectCheck::_exists(const String &p_dependency, String &r_path) const {
	const Vector<String> parts = p_dependency.split("::");
	const String first = parts[0];
	const String fallback = parts.size() >= 3 ? parts[2] : String();
	r_path = fallback.is_empty() ? first : fallback;
	if (first.begins_with("uid://")) {
		const ResourceUID::ID id = ResourceUID::get_singleton()->text_to_id(first);
		if (id != ResourceUID::INVALID_ID && ResourceUID::get_singleton()->has_id(id)) {
			r_path = ResourceUID::get_singleton()->get_id_path(id);
			return ResourceLoader::exists(r_path);
		}
		return false;
	}
	return ResourceLoader::exists(first);
}

void ProjectCheck::_check_dependencies(const String &p_path) {
	List<String> dependencies;
	ResourceLoader::get_dependencies(p_path, &dependencies);
	for (const String &dependency : dependencies) {
		String path;
		if (_exists(dependency, path)) {
			continue;
		}
		if (!path.is_empty() && ResourceLoader::exists(path)) {
			_warning(p_path, vformat("refers to %s by an ID it no longer has; open and save it in the editor to update it.", path));
		} else {
			_error(p_path, vformat("needs %s, which isn't there.", path.is_empty() ? dependency : path));
		}
	}
}

void ProjectCheck::_check_scene(const String &p_path, const Ref<Resource> &p_resource) {
	const Ref<PackedScene> scene = p_resource;
	if (scene.is_null()) {
		return;
	}
	Node *root = scene->instantiate();
	if (root == nullptr) {
		_error(p_path, "can't be made into nodes (see the errors above).");
		return;
	}
	_check_node(p_path, root, root);
	memdelete(root);
}

void ProjectCheck::_check_node(const String &p_path, Node *p_root, Node *p_node) {
	// A node from another scene is checked with that scene.
	if (p_node != p_root && !p_node->get_scene_file_path().is_empty()) {
		return;
	}
	const String where = p_node == p_root ? String(p_node->get_name()) : String(p_root->get_name()) + "/" + String(p_root->get_path_to(p_node));
	for (const String &warning : p_node->get_configuration_warnings()) {
		_warning(p_path, vformat("%s: %s", where, warning));
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_check_node(p_path, p_root, p_node->get_child(i));
	}
}

void ProjectCheck::_finish() {
	int errors = 0;
	int warnings = 0;
	for (const Problem &problem : problems) {
		errors += problem.error ? 1 : 0;
		warnings += problem.error ? 0 : 1;
	}
	print_line(vformat("Checked %d %s: %d %s, %d %s.", checked, checked == 1 ? "file" : "files", errors, errors == 1 ? "error" : "errors", warnings, warnings == 1 ? "warning" : "warnings"));
	for (const String &argument : OS::get_singleton()->get_cmdline_args()) {
		if (argument.begins_with("--test-report=")) {
			_write_report(argument.get_slicec('=', 1));
		}
	}
	get_tree()->quit(errors > 0 ? 1 : 0);
}

// A JUnit XML report: a case for each file with problems, failing when one is an error.
void ProjectCheck::_write_report(const String &p_path) {
	String path = p_path;
	if (path.is_relative_path()) {
		Ref<DirAccess> here = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		path = here->get_current_dir().path_join(path);
	}
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_null()) {
		ERR_PRINT(vformat("Couldn't write the check's report to %s.", path));
		return;
	}
	Vector<String> order;
	HashMap<String, Vector<int>> by_file;
	int failing = 0;
	for (int i = 0; i < problems.size(); i++) {
		if (!by_file.has(problems[i].file)) {
			order.push_back(problems[i].file);
		}
		by_file[problems[i].file].push_back(i);
	}
	for (const String &name : order) {
		for (int index : by_file[name]) {
			if (problems[index].error) {
				failing++;
				break;
			}
		}
	}
	file->store_line("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
	file->store_line(vformat("<testsuites name=\"project check\" tests=\"%d\" failures=\"%d\">", order.size(), failing));
	file->store_line(vformat("\t<testsuite name=\"project check\" tests=\"%d\" failures=\"%d\">", order.size(), failing));
	for (const String &name : order) {
		file->store_line(vformat("\t\t<testcase name=\"%s\" classname=\"project\">", name.xml_escape(true)));
		String errors;
		String warnings;
		for (int index : by_file[name]) {
			(problems[index].error ? errors : warnings) += problems[index].message + "\n";
		}
		if (!errors.is_empty()) {
			file->store_line(vformat("\t\t\t<failure message=\"%s\">%s</failure>", errors.get_slicec('\n', 0).xml_escape(true), errors.xml_escape(true)));
		}
		if (!warnings.is_empty()) {
			file->store_line(vformat("\t\t\t<system-out>%s</system-out>", warnings.xml_escape(true)));
		}
		file->store_line("\t\t</testcase>");
	}
	file->store_line("\t</testsuite>");
	file->store_line("</testsuites>");
}
