#include "console.h"

#include "console_view.h"
#include "debug_draw.h"
#include "profile_recording.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/debugger/engine_debugger.h"
#include "core/io/logger.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/keyboard.h"
#include "core/os/os.h"
#include "core/variant/variant_utility.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

#include <cstdio>

Console *Console::singleton = nullptr;

// Collects what the game prints (from any thread) for the console to show on the main thread.
class ConsoleLogger : public Logger {
	Mutex mutex;
	Vector<Console::Line> pending;

	void _add(const String &p_text, Console::LineKind p_kind) {
		MutexLock lock(mutex);
		if (pending.size() < Console::MAX_OUTPUT_LINES) {
			pending.push_back({ p_text, p_kind });
		}
	}

public:
	bool active = true;

	virtual void logv(const char *p_format, va_list p_list, bool p_err) override {
		if (!active) {
			return;
		}
		char buffer[1024];
		va_list list_copy;
		va_copy(list_copy, p_list);
		vsnprintf(buffer, sizeof(buffer), p_format, list_copy);
		va_end(list_copy);
		String text = String::utf8(buffer).trim_suffix("\n");
		if (text.is_empty()) {
			return;
		}
		_add(text, p_err ? Console::LINE_ERROR : Console::LINE_NORMAL);
	}

	// One line an error: the full location is in the log.
	virtual void log_error(const char *p_function, const char *p_file, int p_line, const char *p_code, const char *p_rationale, bool p_editor_notify, ErrorType p_type, const Vector<Ref<ScriptBacktrace>> &p_script_backtraces) override {
		if (!active) {
			return;
		}
		const String details = String::utf8(p_rationale && *p_rationale ? p_rationale : p_code);
		_add(vformat("%s: %s", error_type_string(p_type), details), p_type == ERR_WARNING ? Console::LINE_WARNING : Console::LINE_ERROR);
	}

	Vector<Console::Line> take() {
		MutexLock lock(mutex);
		Vector<Console::Line> lines = pending;
		pending.clear();
		return lines;
	}
};

Console::Console() {
	singleton = this;
	enabled = GLOBAL_DEF("console/enabled_in_release_builds", false);
#ifdef DEBUG_ENABLED
	enabled = true;
#endif
	// A key's name, as Godot writes it ("QuoteLeft", "F1").
	const String key_name = GLOBAL_DEF("console/open_key", keycode_get_string(Key::QUOTELEFT));
	open_key = find_keycode(key_name);
	if (open_key == Key::NONE) {
		WARN_PRINT(vformat("console/open_key is \"%s\", which isn't a key's name, so the console opens with QuoteLeft.", key_name));
		open_key = Key::QUOTELEFT;
	}
	if (!Engine::get_singleton()->is_editor_hint()) {
		callable_mp(this, &Console::_start).call_deferred();
	}
}

Console::~Console() {
	if (profile != nullptr) {
		memdelete(profile);
	}
	if (logger != nullptr) {
		logger->active = false;
	}
	singleton = nullptr;
}

void Console::_start() {
	SceneTree *tree = SceneTree::get_singleton();
	if (started || tree == nullptr) {
		return;
	}
	started = true;
	tree->connect(SNAME("process_frame"), callable_mp(this, &Console::_process_frame));
	if (enabled) {
		_ensure_view();
	}
}

void Console::_process_frame() {
	if (logger == nullptr) {
		return;
	}
	for (const Line &line : logger->take()) {
		write_line(line.text, line.kind);
	}
}

// The view sits in the tree from the start (it listens for the key that opens it) and builds its
// controls the first time it opens. The log is collected from then on too.
void Console::_ensure_view() {
	if (logger == nullptr) {
		logger = memnew(ConsoleLogger);
		OS::get_singleton()->add_logger(logger);
	}
	if (view_id.is_valid() && ObjectDB::get_instance(view_id) != nullptr) {
		return;
	}
	SceneTree *tree = SceneTree::get_singleton();
	if (tree == nullptr || tree->get_root() == nullptr) {
		return;
	}
	ConsoleView *view = memnew(ConsoleView);
	view_id = view->get_instance_id();
	tree->get_root()->add_child(view, false, Node::INTERNAL_MODE_BACK);
}

void Console::set_enabled(bool p_enabled) {
	enabled = p_enabled;
	if (!enabled) {
		set_visible(false);
	} else if (started) {
		_ensure_view();
	}
}

bool Console::is_enabled() const {
	return enabled;
}

void Console::set_visible(bool p_visible) {
	if (p_visible && !enabled) {
		return;
	}
	if (visible == p_visible) {
		return;
	}
	visible = p_visible;
	ConsoleView *view = Object::cast_to<ConsoleView>(ObjectDB::get_instance(view_id));
	if (view != nullptr) {
		view->set_open(visible);
	}
}

bool Console::is_visible() const {
	return visible;
}

void Console::add_command(const String &p_name, const Callable &p_callable, const String &p_description) {
	ERR_FAIL_COND_MSG(p_name.is_empty() || p_name.contains_char(' '), vformat("A console command's name can't be empty or have spaces: \"%s\".", p_name));
	ERR_FAIL_COND_MSG(!p_callable.is_valid(), vformat("The console command \"%s\" needs a valid callable.", p_name));
	commands[p_name] = { p_callable, p_description };
}

void Console::remove_command(const String &p_name) {
	commands.erase(p_name);
}

namespace {
// Commands every console has. A game's own command of the same name takes their place.
struct BuiltIn {
	const char *name;
	const char *description;
};
const BuiltIn BUILT_INS[] = {
	{ "clear", "Clears the console." },
	{ "help", "Lists the commands." },
	{ "perf", "Shows or hides the performance overlay." },
	{ "profile", "Records the next 10 seconds (or as many as you give) and saves a report of where the time went." },
};

const BuiltIn *built_in(const String &p_name) {
	for (const BuiltIn &each : BUILT_INS) {
		if (p_name == each.name) {
			return &each;
		}
	}
	return nullptr;
}
} // namespace

void Console::_profile(const Vector<String> &p_words) {
	if (profile == nullptr) {
		profile = memnew(ProfileRecording);
	}
	// Starting and stopping the scripts' profiling would stop the editor's own.
	if (EngineDebugger::get_singleton() != nullptr && EngineDebugger::get_singleton()->is_profiling("scripts")) {
		write_line("The editor's profiler is running: use it instead, or stop it first.", LINE_ERROR);
		return;
	}
	if (profile->is_recording()) {
		write_line(vformat("Already profiling: %.0f s to go.", Math::ceil(profile->get_seconds_left())), LINE_ERROR);
		return;
	}
	double seconds = 10.0;
	if (p_words.size() > 1) {
		seconds = p_words[1].to_float();
		if (!p_words[1].is_valid_float() || seconds < 1.0 || seconds > 600.0) {
			write_line("Usage: profile [seconds], from 1 to 600.", LINE_ERROR);
			return;
		}
	}
	write_line(vformat("Profiling for %s s.", String::num(seconds)), LINE_NORMAL);
	profile->start(seconds);
}

bool Console::has_command(const String &p_name) const {
	_forget_freed();
	return commands.has(p_name) || built_in(p_name) != nullptr;
}

// Commands whose node or object has been freed are forgotten.
void Console::_forget_freed() const {
	Vector<String> gone;
	for (const KeyValue<String, Command> &entry : commands) {
		if (!entry.value.callable.is_valid()) {
			gone.push_back(entry.key);
		}
	}
	for (const String &name : gone) {
		commands.erase(name);
	}
}

PackedStringArray Console::get_commands() const {
	_forget_freed();
	Vector<String> names;
	for (const BuiltIn &each : BUILT_INS) {
		names.push_back(each.name);
	}
	for (const KeyValue<String, Command> &entry : commands) {
		if (built_in(entry.key) == nullptr) {
			names.push_back(entry.key);
		}
	}
	names.sort();
	return PackedStringArray(names);
}

PackedStringArray Console::complete(const String &p_partial) const {
	PackedStringArray matches;
	for (const String &name : get_commands()) {
		if (name.begins_with(p_partial)) {
			matches.push_back(name);
		}
	}
	return matches;
}

// Words split at spaces, with quotes ("two words" or 'two words') keeping spaces in one.
Vector<String> Console::_split(const String &p_line) {
	Vector<String> words;
	String word;
	char32_t quote = 0;
	bool in_word = false;
	for (int i = 0; i < p_line.length(); i++) {
		const char32_t c = p_line[i];
		if (quote != 0) {
			if (c == quote) {
				quote = 0;
			} else {
				word += c;
			}
		} else if (c == '"' || c == '\'') {
			quote = c;
			in_word = true;
		} else if (c == ' ' || c == '\t') {
			if (in_word) {
				words.push_back(word);
				word = String();
				in_word = false;
			}
		} else {
			word += c;
			in_word = true;
		}
	}
	if (in_word) {
		words.push_back(word);
	}
	return words;
}

// The arguments a command takes, when its callable is a method whose declaration says (a
// script's or a class's method). A lambda doesn't say; its arguments are guessed from the text.
Vector<Console::Argument> Console::_arguments(const Callable &p_callable, bool &r_known) const {
	Vector<Argument> arguments;
	r_known = false;
	Object *object = p_callable.get_object();
	const StringName method = p_callable.get_method();
	const int count = p_callable.get_argument_count();
	if (object != nullptr && method != StringName()) {
		List<MethodInfo> methods;
		object->get_method_list(&methods);
		for (const MethodInfo &info : methods) {
			if (info.name != method) {
				continue;
			}
			r_known = true;
			const int first_optional = info.arguments.size() - info.default_arguments.size();
			for (int i = 0; i < MIN(count, int(info.arguments.size())); i++) {
				arguments.push_back({ info.arguments[i].name, info.arguments[i].type, i >= first_optional });
			}
			return arguments;
		}
	}
	for (int i = 0; i < count; i++) {
		arguments.push_back({ vformat("arg%d", i + 1), Variant::NIL, false });
	}
	return arguments;
}

bool Console::_convert(const String &p_text, Variant::Type p_type, Variant &r_value) {
	const String text = p_text.strip_edges();
	switch (p_type) {
		case Variant::NIL:
			if (text == "true" || text == "false") {
				r_value = text == "true";
			} else if (text.is_valid_int()) {
				r_value = text.to_int();
			} else if (text.is_valid_float()) {
				r_value = text.to_float();
			} else {
				r_value = p_text;
			}
			return true;
		case Variant::BOOL: {
			const String lower = text.to_lower();
			if (lower == "true" || lower == "1" || lower == "on" || lower == "yes") {
				r_value = true;
				return true;
			}
			if (lower == "false" || lower == "0" || lower == "off" || lower == "no") {
				r_value = false;
				return true;
			}
			return false;
		}
		case Variant::INT:
			r_value = text.to_int();
			return text.is_valid_int();
		case Variant::FLOAT:
			r_value = text.to_float();
			return text.is_valid_float();
		case Variant::STRING:
			r_value = p_text;
			return true;
		case Variant::STRING_NAME:
			r_value = StringName(p_text);
			return true;
		case Variant::VECTOR2:
		case Variant::VECTOR3:
		case Variant::VECTOR2I:
		case Variant::VECTOR3I: {
			// "1,2,3" (spaces allowed after commas); whole numbers for the integer vectors.
			const PackedStringArray parts = text.split(",");
			const bool whole = p_type == Variant::VECTOR2I || p_type == Variant::VECTOR3I;
			const int needed = p_type == Variant::VECTOR2 || p_type == Variant::VECTOR2I ? 2 : 3;
			if (parts.size() != needed) {
				return false;
			}
			real_t values[3] = {};
			for (int i = 0; i < needed; i++) {
				const String part = parts[i].strip_edges();
				if (whole ? !part.is_valid_int() : !part.is_valid_float()) {
					return false;
				}
				values[i] = part.to_float();
			}
			switch (p_type) {
				case Variant::VECTOR2:
					r_value = Vector2(values[0], values[1]);
					break;
				case Variant::VECTOR3:
					r_value = Vector3(values[0], values[1], values[2]);
					break;
				case Variant::VECTOR2I:
					r_value = Vector2i(values[0], values[1]);
					break;
				default:
					r_value = Vector3i(values[0], values[1], values[2]);
					break;
			}
			return true;
		}
		default: {
			// Anything else in GDScript's own notation, such as Color(1, 0, 0).
			Variant parsed = VariantUtilityFunctions::str_to_var(text);
			if (parsed.get_type() != p_type) {
				return false;
			}
			r_value = parsed;
			return true;
		}
	}
}

String Console::_usage(const String &p_name) const {
	const Command *command = commands.getptr(p_name);
	if (command == nullptr) {
		return p_name;
	}
	bool known = false;
	String usage = p_name;
	for (const Argument &argument : _arguments(command->callable, known)) {
		const String described = argument.type == Variant::NIL ? argument.name : vformat("%s: %s", argument.name, Variant::get_type_name(argument.type));
		usage += argument.optional ? vformat(" [%s]", described) : vformat(" <%s>", described);
	}
	return usage;
}

void Console::_help() {
	for (const String &name : get_commands()) {
		String description;
		if (commands.has(name)) {
			description = commands[name].description;
		} else if (const BuiltIn *each = built_in(name)) {
			description = each->description;
		}
		const String usage = _usage(name);
		write_line(description.is_empty() ? usage : vformat("%s  %s", usage, description), LINE_NORMAL);
	}
}

Variant Console::run(const String &p_line) {
	const String line = p_line.strip_edges();
	if (line.is_empty()) {
		return Variant();
	}
	if (history.is_empty() || history[history.size() - 1] != line) {
		history.push_back(line);
		if (history.size() > MAX_HISTORY) {
			history.remove_at(0);
		}
	}
	write_line("> " + line, LINE_COMMAND);

	_forget_freed();
	const Vector<String> words = _split(line);
	const String name = words[0];
	const Command *command = commands.getptr(name);
	if (command == nullptr) {
		if (name == "help") {
			_help();
		} else if (name == "clear") {
			clear();
		} else if (name == "perf") {
			DebugDraw *draw = DebugDraw::get_singleton();
			if (draw != nullptr) {
				draw->set_performance_overlay(!draw->is_performance_overlay());
			}
		} else if (name == "profile") {
			_profile(words);
		} else {
			write_line(vformat("No command called \"%s\". Type help to see them all.", name), LINE_ERROR);
		}
		return Variant();
	}

	bool known = false;
	const Vector<Argument> arguments = _arguments(command->callable, known);
	const int given = words.size() - 1;
	int required = 0;
	for (const Argument &argument : arguments) {
		required += argument.optional ? 0 : 1;
	}
	if (given < required || given > arguments.size()) {
		write_line("Usage: " + _usage(name), LINE_ERROR);
		return Variant();
	}
	Vector<Variant> values;
	values.resize(given);
	for (int i = 0; i < given; i++) {
		if (!_convert(words[i + 1], arguments[i].type, values.write[i])) {
			write_line(vformat("\"%s\" isn't a valid %s. Usage: %s", words[i + 1], Variant::get_type_name(arguments[i].type), _usage(name)), LINE_ERROR);
			return Variant();
		}
	}
	Vector<const Variant *> pointers;
	pointers.resize(given);
	for (int i = 0; i < given; i++) {
		pointers.write[i] = &values[i];
	}
	Variant result;
	Callable::CallError error;
	command->callable.callp(pointers.ptrw(), given, result, error);
	if (error.error != Callable::CallError::CALL_OK) {
		write_line(Variant::get_callable_error_text(command->callable, pointers.ptrw(), given, error), LINE_ERROR);
		return Variant();
	}
	if (result.get_type() != Variant::NIL) {
		write(result.stringify());
	}
	return result;
}

void Console::write(const String &p_text) {
	for (const String &line : p_text.split("\n")) {
		write_line(line, LINE_NORMAL);
	}
}

void Console::write_line(const String &p_text, LineKind p_kind) {
	output.push_back({ p_text, p_kind });
	output_serial++;
	if (output.size() > MAX_OUTPUT_LINES) {
		output.remove_at(0);
	}
}

String Console::get_text() const {
	String text;
	for (const Line &line : output) {
		text += line.text + "\n";
	}
	return text;
}

void Console::clear() {
	output.clear();
	output_serial++;
	ConsoleView *view = Object::cast_to<ConsoleView>(ObjectDB::get_instance(view_id));
	if (view != nullptr) {
		view->clear_output();
	}
}

void Console::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &Console::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &Console::is_enabled);
	ClassDB::bind_method(D_METHOD("set_visible", "visible"), &Console::set_visible);
	ClassDB::bind_method(D_METHOD("is_visible"), &Console::is_visible);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "visible"), "set_visible", "is_visible");

	ClassDB::bind_method(D_METHOD("add_command", "name", "callable", "description"), &Console::add_command, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("remove_command", "name"), &Console::remove_command);
	ClassDB::bind_method(D_METHOD("has_command", "name"), &Console::has_command);
	ClassDB::bind_method(D_METHOD("get_commands"), &Console::get_commands);
	ClassDB::bind_method(D_METHOD("run", "line"), &Console::run);
	ClassDB::bind_method(D_METHOD("write", "text"), &Console::write);
	ClassDB::bind_method(D_METHOD("clear"), &Console::clear);
	ClassDB::bind_method(D_METHOD("get_text"), &Console::get_text);
}
