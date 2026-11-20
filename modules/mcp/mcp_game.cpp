#include "mcp_game.h"

#include "mcp_log_buffer.h"

#include "core/crypto/crypto_core.h"
#include "core/debugger/engine_debugger.h"
#include "core/math/expression.h"
#include "core/object/callable_mp.h"
#include "main/performance.h"
#include "modules/debug_tools/console.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/3d/node_3d.h"
#include "scene/main/canvas_item.h"

namespace McpGame {

namespace {
const char *CAPTURE = "purr_mcp";
bool registered = false;

String describe(Node *p_node, int p_depth, int p_max_depth, int &r_count) {
	r_count++;
	String line = String("  ").repeat(p_depth) + p_node->get_name() + ": " + p_node->get_class();
	const Ref<Resource> script = p_node->get_script();
	if (script.is_valid() && !script->get_path().is_empty()) {
		line += " (script " + script->get_path() + ")";
	}
	if (const CanvasItem *item = Object::cast_to<CanvasItem>(p_node)) {
		line += item->is_visible() ? String() : " [hidden]";
	} else if (const Node3D *spatial = Object::cast_to<Node3D>(p_node)) {
		line += spatial->is_visible() ? vformat(" at %s", spatial->get_global_position()) : String(" [hidden]");
	}
	if (p_depth >= p_max_depth) {
		if (p_node->get_child_count() > 0) {
			line += vformat(" (%d children)", p_node->get_child_count());
		}
		return line + "\n";
	}
	line += "\n";
	for (int i = 0; i < p_node->get_child_count() && r_count < 2000; i++) {
		line += describe(p_node->get_child(i), p_depth + 1, p_max_depth, r_count);
	}
	return line;
}

Dictionary text_result(const String &p_text) {
	Dictionary result;
	result["text"] = p_text;
	return result;
}

Dictionary error_result(const String &p_text) {
	Dictionary result;
	result["error"] = p_text;
	return result;
}

Dictionary answer(const String &p_request, const Dictionary &p_params) {
	SceneTree *tree = SceneTree::get_singleton();
	if (tree == nullptr) {
		return error_result("The game has no scene tree.");
	}
	if (p_request == "tree") {
		Node *from = tree->get_root()->get_node_or_null(NodePath(String(p_params.get("path", "/root"))));
		if (from == nullptr) {
			return error_result("No node at " + String(p_params.get("path", "")) + ".");
		}
		int count = 0;
		return text_result(describe(from, 0, CLAMP(int(p_params.get("depth", 4)), 0, 50), count));
	}
	if (p_request == "log") {
		if (McpLogBuffer::singleton == nullptr) {
			return error_result("The game's log isn't kept.");
		}
		return text_result(McpLogBuffer::singleton->get_lines(CLAMP(int(p_params.get("lines", 100)), 1, 1000), bool(p_params.get("problems_only", false))));
	}
	if (p_request == "monitors") {
		Performance *performance = Performance::get_singleton();
		String text;
		Dictionary values;
		for (int i = 0; i < Performance::MONITOR_MAX; i++) {
			const Performance::Monitor monitor = Performance::Monitor(i);
			values[performance->get_monitor_name(monitor)] = performance->get_monitor(monitor);
		}
		for (const Variant &name : performance->get_custom_monitor_names()) {
			values[name] = performance->get_custom_monitor(name);
		}
		for (const KeyValue<Variant, Variant> &value : values) {
			text += vformat("%s = %s\n", value.key, value.value);
		}
		Dictionary result = text_result(text);
		result["monitors"] = values;
		return result;
	}
	if (p_request == "console") {
		Console *console = Console::get_singleton();
		if (console == nullptr) {
			return error_result("The game has no console.");
		}
		// Each line written moves the serial on by one: the new ones are the last that many.
		const int before = console->get_output_serial();
		const Variant returned = console->run(String(p_params.get("command", "")));
		const Vector<Console::Line> &output = console->get_output();
		const int written = MIN(console->get_output_serial() - before, output.size());
		String text;
		for (int i = output.size() - written; i < output.size(); i++) {
			text += output[i].text + "\n";
		}
		if (returned.get_type() != Variant::NIL) {
			text += "=> " + returned.stringify() + "\n";
		}
		return text_result(text.is_empty() ? String("(no output)") : text);
	}
	if (p_request == "eval") {
		Node *base = tree->get_root()->get_node_or_null(NodePath(String(p_params.get("node", "/root"))));
		if (base == nullptr) {
			return error_result("No node at " + String(p_params.get("node", "")) + ".");
		}
		Ref<Expression> expression;
		expression.instantiate();
		if (expression->parse(p_params.get("expression", "")) != OK) {
			return error_result("Doesn't parse: " + expression->get_error_text());
		}
		const Variant value = expression->execute(Array(), base, false);
		if (expression->has_execute_failed()) {
			return error_result("Failed: " + expression->get_error_text());
		}
		return text_result(value.stringify());
	}
	if (p_request == "screenshot") {
		const Ref<Image> image = tree->get_root()->get_texture()->get_image();
		if (image.is_null()) {
			return error_result("Nothing to take: the game draws nothing (headless?).");
		}
		const PackedByteArray png = image->save_png_to_buffer();
		Dictionary result = text_result(vformat("The game's window, %dx%d.", image->get_width(), image->get_height()));
		result["png"] = CryptoCore::b64_encode_str(png.ptr(), png.size());
		return result;
	}
	return error_result("Unknown request: " + p_request);
}

Error capture(void *p_user, const String &p_message, const Array &p_args, bool &r_captured) {
	r_captured = true;
	const Variant key = p_args.size() > 0 ? p_args[0] : Variant();
	const Dictionary params = p_args.size() > 1 && p_args[1].get_type() == Variant::DICTIONARY ? Dictionary(p_args[1]) : Dictionary();
	EngineDebugger::get_singleton()->send_message("purr_mcp:reply", Array({ key, answer(p_message, params) }));
	return OK;
}

void start() {
	if (!registered && EngineDebugger::is_active()) {
		EngineDebugger::register_message_capture(CAPTURE, EngineDebugger::Capture(nullptr, &capture));
		registered = true;
	}
}
} // namespace

void start_if_debugged() {
	McpLogBuffer::install();
	callable_mp_static(&start).call_deferred();
}

void stop() {
	if (registered && EngineDebugger::is_active()) {
		EngineDebugger::unregister_message_capture(CAPTURE);
	}
	registered = false;
}

} // namespace McpGame
