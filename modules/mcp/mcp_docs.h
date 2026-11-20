#pragma once

// What an AI assistant can read about Purr and Godot: Purr's guides (the docs site's pages, built
// into the editor) and the class reference of every class, Godot's and Purr's, as the editor's
// help has it. Both are searched together, and read by id: "guide:modules/water.md",
// "guide:modules/water.md#waves", "class:Water3D" or "class:Water3D.get_height_at".

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

namespace McpDocs {

struct Hit {
	String id;
	String title;
	String snippet;
	bool purr = false;
	float score = 0.0;
};

Vector<Hit> search(const String &p_query, int p_limit);
// The text of a guide, a section of one, a class or a class's member; empty if there's none.
String read(const String &p_id);

// The guides as MCP resources (purr://docs/<path>).
Array guide_resources();
Dictionary read_guide_resource(const String &p_uri);

bool is_purr_class(const String &p_class);
void cleanup();

} // namespace McpDocs
