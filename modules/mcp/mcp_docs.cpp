#include "mcp_docs.h"

#include "mcp_guides.gen.h"
#include "mcp_purr_classes.gen.h"

#include "core/templates/hash_set.h"
#include "editor/doc/doc_data_compressed.gen.h"
#include "editor/doc/doc_tools.h"

namespace McpDocs {

namespace {

struct Entry {
	String id;
	String title;
	String title_lower;
	String name_lower; // The class or member's own name, for exact matches.
	String text; // Plain text, for snippets.
	String text_lower;
	bool purr = false;
};

struct Index {
	DocTools docs;
	Vector<Entry> entries;
	HashSet<String> purr_classes;
};

Index *docs_index = nullptr; // Made on first use (loading the reference takes a moment), freed by cleanup().

// The reference's BBCode as Markdown an assistant reads easily: code in backticks (GDScript's
// block where there's a choice of languages), links and references to classes and members as
// their names.
String plain(const String &p_bbcode) {
	String text;
	const int length = p_bbcode.length();
	int at = 0;
	while (at < length) {
		const int open = p_bbcode.find("[", at);
		if (open < 0) {
			text += p_bbcode.substr(at);
			break;
		}
		text += p_bbcode.substr(at, open - at);
		const int close = p_bbcode.find("]", open);
		if (close < 0) {
			text += p_bbcode.substr(open);
			break;
		}
		const String tag = p_bbcode.substr(open + 1, close - open - 1);
		const String name = tag.get_slicec(' ', 0).get_slicec('=', 0);
		at = close + 1;
		auto until = [&](const String &p_end) {
			const int end = p_bbcode.find(p_end, at);
			const String inside = end < 0 ? p_bbcode.substr(at) : p_bbcode.substr(at, end - at);
			at = end < 0 ? length : end + p_end.length();
			return inside;
		};
		if (name == "codeblocks") {
			const String inside = until("[/codeblocks]");
			const int from = inside.find("[gdscript]");
			const int to = inside.find("[/gdscript]");
			if (from >= 0 && to > from) {
				text += "\n```gdscript" + inside.substr(from + 10, to - from - 10) + "```\n";
			}
		} else if (name == "codeblock") {
			text += "\n```gdscript" + until("[/codeblock]") + "```\n";
		} else if (name == "code" || name == "kbd") {
			text += "`" + until("[/" + name + "]") + "`";
		} else if (name == "url") {
			const String link = tag.get_slicec('=', 1);
			text += until("[/url]") + (link.is_empty() ? String() : " (" + link + ")");
		} else if (name == "br") {
			text += "\n";
		} else if (name == "member" || name == "method" || name == "signal" || name == "constant" || name == "enum" || name == "param" || name == "annotation" || name == "theme_item" || name == "constructor" || name == "operator") {
			text += "`" + tag.substr(name.length() + 1) + "`";
		} else if (name == "b" || name == "i" || name == "u" || name == "s" || name == "center" || name.begins_with("/")) {
			// Emphasis: left out.
		} else if (name == "lb") {
			text += "[";
		} else if (name == "rb") {
			text += "]";
		} else if (!name.is_empty() && (is_ascii_upper_case(name[0]) || name[0] == '@') && !name.contains(" ")) {
			text += "`" + name + "`"; // A class.
		} else {
			text += "[" + tag + "]";
		}
	}
	// The reference's lines are indented for its XML: that much comes off each, keeping the
	// indentation within code.
	PackedStringArray lines = text.split("\n");
	int shared = INT_MAX;
	for (const String &line : lines) {
		if (!line.strip_edges().is_empty()) {
			int tabs = 0;
			while (tabs < line.length() && line[tabs] == '\t') {
				tabs++;
			}
			shared = MIN(shared, tabs);
		}
	}
	for (int i = 0; i < lines.size(); i++) {
		lines.write[i] = lines[i].substr(MIN(shared, lines[i].length()));
	}
	return String("\n").join(lines).strip_edges();
}

String one_line(const String &p_text) {
	String text = p_text.replace("\n", " ").replace("\t", " ");
	while (text.contains("  ")) {
		text = text.replace("  ", " ");
	}
	return text.strip_edges();
}

// How often a word begins in a text (so "rain" isn't found in "terrain"), up to three.
int count_word(const String &p_text, const String &p_word) {
	int count = 0;
	for (int at = p_text.find(p_word); at >= 0 && count < 3; at = p_text.find(p_word, at + p_word.length())) {
		if (at == 0 || !is_ascii_alphanumeric_char(p_text[at - 1])) {
			count++;
		}
	}
	return count;
}

// As the docs site makes a heading's anchor.
String anchor(const String &p_heading) {
	String slug;
	const String lower = p_heading.to_lower();
	for (int i = 0; i < lower.length(); i++) {
		const char32_t c = lower[i];
		if (is_ascii_alphanumeric_char(c)) {
			slug += c;
		} else if (c == ' ' || c == '-') {
			slug += '-';
		}
	}
	return slug;
}

String signature(const DocData::MethodDoc &p_method) {
	PackedStringArray arguments;
	for (const DocData::ArgumentDoc &argument : p_method.arguments) {
		String text = argument.name + ": " + (argument.enumeration.is_empty() ? argument.type : argument.enumeration);
		if (!argument.default_value.is_empty()) {
			text += " = " + argument.default_value;
		}
		arguments.push_back(text);
	}
	String result = p_method.name + "(" + String(", ").join(arguments) + ")";
	const String returns = p_method.return_enum.is_empty() ? p_method.return_type : p_method.return_enum;
	if (!returns.is_empty() && returns != "void") {
		result += " -> " + returns;
	}
	if (!p_method.qualifiers.is_empty()) {
		result += " " + p_method.qualifiers;
	}
	return result;
}

void add(Index *p_index, const String &p_id, const String &p_title, const String &p_name, const String &p_text, bool p_purr) {
	Entry entry;
	entry.id = p_id;
	entry.title = p_title;
	entry.title_lower = p_title.to_lower();
	entry.name_lower = p_name.to_lower();
	entry.text = p_text;
	entry.text_lower = p_text.to_lower();
	entry.purr = p_purr;
	p_index->entries.push_back(entry);
}

Index *get_index() {
	if (docs_index != nullptr) {
		return docs_index;
	}
	docs_index = memnew(Index);
	for (const char *name : MCP_PURR_CLASSES) {
		docs_index->purr_classes.insert(name);
	}
	// Purr's guides, a section each.
	for (const McpGuide &guide : MCP_GUIDES) {
		const String path = guide.path;
		const PackedStringArray lines = String::utf8(guide.text).split("\n");
		String page_title = path;
		String heading;
		String body;
		auto flush = [&]() {
			if (!heading.is_empty() || !body.strip_edges().is_empty()) {
				const String id = "guide:" + path + (heading.is_empty() ? String() : "#" + anchor(heading));
				const String title = heading.is_empty() ? page_title : page_title + ": " + heading;
				add(docs_index, id, title, heading, body.strip_edges(), true);
			}
			body = String();
		};
		bool in_code = false;
		for (const String &line : lines) {
			in_code = line.begins_with("```") ? !in_code : in_code;
			if (!in_code && line.begins_with("# ")) {
				page_title = line.substr(2).strip_edges();
				continue;
			}
			if (!in_code && (line.begins_with("## ") || line.begins_with("### "))) {
				flush();
				heading = line.substr(line.find(" ") + 1).strip_edges();
				continue;
			}
			body += line + "\n";
		}
		flush();
	}
	// Every class, and each of its members.
	docs_index->docs.load_compressed(_doc_data_compressed, _doc_data_compressed_size, _doc_data_uncompressed_size);
	for (const KeyValue<String, DocData::ClassDoc> &item : docs_index->docs.class_list) {
		const DocData::ClassDoc &doc = item.value;
		const bool purr = docs_index->purr_classes.has(doc.name);
		add(docs_index, "class:" + doc.name, doc.name, doc.name, doc.brief_description + "\n" + doc.description, purr);
		for (const DocData::PropertyDoc &property : doc.properties) {
			add(docs_index, "class:" + doc.name + "." + property.name, doc.name + "." + property.name, property.name, property.description, purr);
		}
		for (const DocData::MethodDoc &method : doc.methods) {
			add(docs_index, "class:" + doc.name + "." + method.name, doc.name + "." + method.name + "()", method.name, method.description, purr);
		}
		for (const DocData::MethodDoc &signal : doc.signals) {
			add(docs_index, "class:" + doc.name + "." + signal.name, doc.name + "." + signal.name + " (signal)", signal.name, signal.description, purr);
		}
	}
	return docs_index;
}

String snippet(const Entry &p_entry, const PackedStringArray &p_words) {
	const String text_plain = one_line(p_entry.id.begins_with("class:") ? plain(p_entry.text) : p_entry.text);
	const String lower = text_plain.to_lower();
	int at = -1;
	for (const String &word : p_words) {
		at = lower.find(word);
		if (at >= 0) {
			break;
		}
	}
	const int from = MAX(0, at - 60);
	String text = text_plain.substr(from, 200);
	if (from > 0) {
		text = "..." + text;
	}
	return text;
}

const DocData::ClassDoc *find_class(const String &p_name) {
	return get_index()->docs.class_list.getptr(p_name);
}

String class_text(const DocData::ClassDoc &p_doc) {
	String text = "# " + p_doc.name + (is_purr_class(p_doc.name) ? " (Purr)" : "");
	if (!p_doc.inherits.is_empty()) {
		text += ", inherits " + p_doc.inherits;
	}
	text += "\n\n" + plain(p_doc.brief_description) + "\n\n" + plain(p_doc.description) + "\n";
	if (!p_doc.properties.is_empty()) {
		text += "\n## Properties\n";
		for (const DocData::PropertyDoc &property : p_doc.properties) {
			text += "- " + property.name + ": " + (property.enumeration.is_empty() ? property.type : property.enumeration);
			if (!property.default_value.is_empty()) {
				text += " = " + property.default_value;
			}
			text += ". " + one_line(plain(property.description)) + "\n";
		}
	}
	if (!p_doc.methods.is_empty()) {
		text += "\n## Methods\n";
		for (const DocData::MethodDoc &method : p_doc.methods) {
			text += "- " + signature(method) + ". " + one_line(plain(method.description)) + "\n";
		}
	}
	if (!p_doc.signals.is_empty()) {
		text += "\n## Signals\n";
		for (const DocData::MethodDoc &signal : p_doc.signals) {
			text += "- " + signature(signal) + ". " + one_line(plain(signal.description)) + "\n";
		}
	}
	if (!p_doc.constants.is_empty()) {
		text += "\n## Constants\n";
		for (const DocData::ConstantDoc &constant : p_doc.constants) {
			text += "- " + (constant.enumeration.is_empty() ? String() : constant.enumeration + ".") + constant.name + " = " + constant.value + ". " + one_line(plain(constant.description)) + "\n";
		}
	}
	return text;
}

String member_text(const DocData::ClassDoc &p_doc, const String &p_member) {
	for (const DocData::PropertyDoc &property : p_doc.properties) {
		if (property.name == p_member) {
			String text = "# " + p_doc.name + "." + property.name + ": " + (property.enumeration.is_empty() ? property.type : property.enumeration);
			if (!property.default_value.is_empty()) {
				text += " = " + property.default_value;
			}
			return text + "\n\n" + plain(property.description);
		}
	}
	for (const Vector<DocData::MethodDoc> *list : { &p_doc.methods, &p_doc.signals, &p_doc.constructors }) {
		for (const DocData::MethodDoc &method : *list) {
			if (method.name == p_member) {
				return "# " + p_doc.name + "." + signature(method) + "\n\n" + plain(method.description);
			}
		}
	}
	for (const DocData::ConstantDoc &constant : p_doc.constants) {
		if (constant.name == p_member) {
			return "# " + p_doc.name + "." + constant.name + " = " + constant.value + "\n\n" + plain(constant.description);
		}
	}
	return String();
}

String guide_text(const String &p_path, const String &p_anchor) {
	for (const McpGuide &guide : MCP_GUIDES) {
		if (p_path != guide.path) {
			continue;
		}
		const String text = String::utf8(guide.text);
		if (p_anchor.is_empty()) {
			return text;
		}
		// From the heading to the next of the same level or higher.
		const PackedStringArray lines = text.split("\n");
		String result;
		int level = 0;
		bool in_code = false;
		for (const String &line : lines) {
			in_code = line.begins_with("```") ? !in_code : in_code;
			const int hashes = (!in_code && line.begins_with("#")) ? line.find(" ") : 0;
			if (level == 0) {
				if (hashes > 0 && anchor(line.substr(hashes + 1)) == p_anchor) {
					level = hashes;
					result = line + "\n";
				}
				continue;
			}
			if (hashes > 0 && hashes <= level) {
				break;
			}
			result += line + "\n";
		}
		return result.strip_edges();
	}
	return String();
}

} // namespace

bool is_purr_class(const String &p_class) {
	return get_index()->purr_classes.has(p_class);
}

Vector<Hit> search(const String &p_query, int p_limit) {
	Index *found = get_index();
	PackedStringArray words;
	for (const String &word : p_query.to_lower().split(" ", false)) {
		if (word.length() >= 2) {
			words.push_back(word);
		}
	}
	Vector<Hit> hits;
	if (words.is_empty()) {
		return hits;
	}
	const String whole = String(" ").join(words);
	LocalVector<Pair<float, int>> scored;
	for (int i = 0; i < found->entries.size(); i++) {
		const Entry &entry = found->entries[i];
		float score = 0.0f;
		int matched = 0;
		for (const String &word : words) {
			float word_score = 0.0f;
			if (entry.name_lower == word) {
				word_score += 25.0f;
			} else if (count_word(entry.title_lower, word) > 0 || count_word(entry.name_lower.replace("_", " "), word) > 0) {
				word_score += 10.0f;
			}
			word_score += count_word(entry.text_lower, word);
			matched += word_score > 0.0f ? 1 : 0;
			score += word_score;
		}
		if (score <= 0.0f) {
			continue;
		}
		if (matched == words.size()) {
			score *= 2.0f;
		}
		if (words.size() > 1 && entry.text_lower.contains(whole)) {
			score += 10.0f;
		}
		if (entry.purr) {
			score += 3.0f; // A Purr feature over Godot's, all else equal.
		}
		scored.push_back(Pair<float, int>(score, i));
	}
	struct Higher {
		bool operator()(const Pair<float, int> &p_a, const Pair<float, int> &p_b) const { return p_a.first > p_b.first; }
	};
	scored.sort_custom<Higher>();
	for (uint32_t i = 0; i < MIN(scored.size(), uint32_t(MAX(p_limit, 1))); i++) {
		const Entry &entry = found->entries[scored[i].second];
		Hit hit;
		hit.id = entry.id;
		hit.title = entry.title;
		hit.snippet = snippet(entry, words);
		hit.purr = entry.purr;
		hit.score = scored[i].first;
		hits.push_back(hit);
	}
	return hits;
}

String read(const String &p_id) {
	if (p_id.begins_with("guide:")) {
		const String rest = p_id.substr(6);
		const int hash = rest.find("#");
		return hash < 0 ? guide_text(rest, String()) : guide_text(rest.substr(0, hash), rest.substr(hash + 1));
	}
	const String name = p_id.begins_with("class:") ? p_id.substr(6) : p_id;
	const int dot = name.find(".");
	const DocData::ClassDoc *doc = find_class(dot < 0 ? name : name.substr(0, dot));
	if (doc == nullptr) {
		return String();
	}
	return dot < 0 ? class_text(*doc) : member_text(*doc, name.substr(dot + 1));
}

Array guide_resources() {
	Array resources;
	for (const McpGuide &guide : MCP_GUIDES) {
		const String text = String::utf8(guide.text);
		String title = guide.path;
		if (text.begins_with("# ")) {
			title = text.get_slicec('\n', 0).substr(2).strip_edges();
		}
		Dictionary resource;
		resource["uri"] = String("purr://docs/") + guide.path;
		resource["name"] = guide.path;
		resource["title"] = title;
		resource["mimeType"] = "text/markdown";
		resources.push_back(resource);
	}
	return resources;
}

Dictionary read_guide_resource(const String &p_uri) {
	if (!p_uri.begins_with("purr://docs/")) {
		return Dictionary();
	}
	const String text = guide_text(p_uri.substr(12), String());
	if (text.is_empty()) {
		return Dictionary();
	}
	Dictionary contents;
	contents["uri"] = p_uri;
	contents["mimeType"] = "text/markdown";
	contents["text"] = text;
	return contents;
}

void cleanup() {
	if (docs_index != nullptr) {
		memdelete(docs_index);
		docs_index = nullptr;
	}
}

} // namespace McpDocs
