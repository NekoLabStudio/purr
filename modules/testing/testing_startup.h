#pragma once

// Runs the tests or checks the project instead of the game, when Purr is started with
// --run-tests or --check-project. Godot loads the main scene by its path after modules start,
// so the main scene is swapped for an empty one made here (held in the resource cache), and the
// runner joins the tree on the first frame.

#include "core/string/ustring.h"

class TestingStartup {
	static void _start(bool p_check);

public:
	static void take_over_if_asked();
	static void release();
	// The project's own main scene, from before it was swapped.
	static String get_main_scene();
};
