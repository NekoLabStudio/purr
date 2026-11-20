#include "mcp_docs.h"
#include "mcp_tool.h"

namespace {

McpResult search_docs(const Dictionary &p_arguments) {
	const String query = McpTools::text(p_arguments, "query");
	if (query.strip_edges().is_empty()) {
		return McpResult::failed("Give a query: what you're looking for, in a few words.");
	}
	const Vector<McpDocs::Hit> hits = McpDocs::search(query, CLAMP(McpTools::integer(p_arguments, "limit", 10), 1, 50));
	McpResult result;
	Array found;
	if (hits.is_empty()) {
		result.text = "Nothing found for \"" + query + "\". Try other words, or a class name.";
	}
	for (const McpDocs::Hit &hit : hits) {
		result.text += vformat("%s%s  [%s]\n  %s\n", hit.title, hit.purr ? " (Purr)" : "", hit.id, hit.snippet);
		Dictionary entry;
		entry["id"] = hit.id;
		entry["title"] = hit.title;
		entry["purr"] = hit.purr;
		entry["snippet"] = hit.snippet;
		found.push_back(entry);
	}
	Dictionary structured;
	structured["results"] = found;
	result.structured = structured;
	return result;
}

McpResult read_doc(const Dictionary &p_arguments) {
	const String id = McpTools::text(p_arguments, "id").strip_edges();
	const String text = McpDocs::read(id);
	if (text.is_empty()) {
		return McpResult::failed("No document \"" + id + "\". Ids look like guide:modules/water.md, guide:modules/water.md#waves, class:Water3D or class:Water3D.get_height_at: search_docs gives them.");
	}
	McpResult result;
	result.text = text;
	return result;
}

} // namespace

void McpTools::add_docs_tools(Vector<McpTool> &r_tools) {
	McpTool search;
	search.name = "search_docs";
	search.title = "Search Purr's docs";
	search.description = "Searches Purr's guides and the class reference of every class, Purr's and Godot's, and lists the best matches with their ids. "
						 "Use it before writing a feature a game commonly needs (saving, achievements, weather, cameras, controls, VR, terrain, tests): Purr may already have it. "
						 "Purr's results are marked (Purr).";
	search.input_schema = McpTool::schema({
			{ "query", "string", "What to look for, in a few words: a feature, a class or member name, a setting.", true },
			{ "limit", "integer", "How many results, 10 if not given (at most 50)." },
	});
	search.run = &search_docs;
	r_tools.push_back(search);

	McpTool read;
	read.name = "read_doc";
	read.title = "Read a doc";
	read.description = "Reads a guide (guide:modules/water.md), one of its sections (guide:modules/water.md#waves), a class's whole reference (class:Water3D, "
					   "or any Godot class such as class:Node3D) or one member's (class:Water3D.get_height_at). search_docs gives ids.";
	read.input_schema = McpTool::schema({
			{ "id", "string", "The document's id, as search_docs gives it. A bare class name works too.", true },
	});
	read.run = &read_doc;
	r_tools.push_back(read);
}
