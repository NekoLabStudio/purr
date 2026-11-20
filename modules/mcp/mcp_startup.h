#pragma once

// Starts the MCP server instead of the game when Purr's started with --mcp: the game's main scene
// gives way to an empty one and its autoloads are left out (in memory only), so none of the game's
// own code runs in the server, and the server joins the tree on the first frame.

#include "core/variant/dictionary.h"

namespace McpStartup {

void take_over_if_asked();
// The project's main scene and autoloads (name: path), as they were before the server set them aside.
String get_main_scene();
Dictionary get_autoloads();
void release();

} // namespace McpStartup
