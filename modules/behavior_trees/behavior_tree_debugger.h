#pragma once

// The game's side of the Behavior Tree dock's live view: when the editor watches a tree, players
// running that tree send their tasks' statuses a few times a second. Nothing is sent otherwise.

#include "core/string/ustring.h"

class Array;
class BehaviorTreePlayer;

class BehaviorTreeDebugger {
	static String watched_path;

	static Error _capture(void *p_user, const String &p_message, const Array &p_data, bool &r_captured);

public:
	static constexpr const char *CAPTURE = "behavior_trees";

	static void initialize();
	static void finalize();
	static bool is_watching(const String &p_tree_path);
	static void report(BehaviorTreePlayer *p_player);
};
