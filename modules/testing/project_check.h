#pragma once

// ProjectCheck: the scene `purr --check-project` runs instead of the game's. It goes through the
// project's files and reports scripts that don't compile, scenes and resources that don't load
// or point at files that aren't there, scenes that can't be made, and nodes' configuration
// warnings (the ones the editor shows in the scene tree), then quits with 1 if there were errors.

#include "scene/main/node.h"

class ProjectCheck : public Node {
	GDCLASS(ProjectCheck, Node);

	struct Problem {
		String file;
		String message;
		bool error = true;
	};

	Vector<Problem> problems;
	PackedStringArray excluded;
	int checked = 0;

	void _error(const String &p_file, const String &p_message);
	void _warning(const String &p_file, const String &p_message);
	void _check_settings();
	void _check_folder(const String &p_folder);
	void _check_file(const String &p_path);
	void _check_dependencies(const String &p_path);
	void _check_scene(const String &p_path, const Ref<Resource> &p_resource);
	void _check_node(const String &p_path, Node *p_root, Node *p_node);
	bool _exists(const String &p_dependency, String &r_path) const;
	void _finish();
	void _write_report(const String &p_path);

protected:
	void _notification(int p_what);

public:
	ProjectCheck();
};
