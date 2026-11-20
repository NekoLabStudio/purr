#pragma once

// Console: an in-game console. The game registers commands (a name, a callable and what it
// does), and typing one runs it, with its arguments converted to the types the method declares.
// It opens with a key (backquote by default), shows the game's printed output, errors and
// warnings, and keeps a history. On in debug builds; in release builds only if the game turns it
// on.

#include "core/object/object.h"
#include "core/object/object_id.h"
#include "core/os/keyboard.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/variant/callable.h"

class ConsoleLogger;
class ProfileRecording;

class Console : public Object {
	GDCLASS(Console, Object);

	static Console *singleton;

	struct Command {
		Callable callable;
		String description;
	};

	struct Argument {
		String name;
		Variant::Type type = Variant::NIL;
		bool optional = false;
	};

public:
	enum LineKind {
		LINE_NORMAL,
		LINE_COMMAND, // What was typed, echoed.
		LINE_WARNING,
		LINE_ERROR,
	};

	struct Line {
		String text;
		LineKind kind = LINE_NORMAL;
	};

private:
	mutable HashMap<String, Command> commands; // Mutable so lookups can forget freed commands.
	Vector<Line> output;
	int output_serial = 0; // Lines ever written, so the view knows what's new.
	Vector<String> history;
	ObjectID view_id;
	ConsoleLogger *logger = nullptr; // Owned by OS, which outlives this.
	ProfileRecording *profile = nullptr;
	bool enabled = true;
	bool started = false;
	bool visible = false;
	Key open_key = Key::NONE;

	void _start();
	void _process_frame();
	void _tree_exiting();
	void _ensure_view();
	Vector<Argument> _arguments(const Callable &p_callable, bool &r_known) const;
	String _usage(const String &p_name) const;
	static Vector<String> _split(const String &p_line);
	static bool _convert(const String &p_text, Variant::Type p_type, Variant &r_value);
	void _help();
	void _profile(const Vector<String> &p_words);
	void _forget_freed() const;

protected:
	static void _bind_methods();

public:
	static Console *get_singleton() { return singleton; }

	static constexpr int MAX_OUTPUT_LINES = 1000;
	static constexpr int MAX_HISTORY = 100;

	void set_enabled(bool p_enabled);
	bool is_enabled() const;
	void set_visible(bool p_visible);
	bool is_visible() const;

	void add_command(const String &p_name, const Callable &p_callable, const String &p_description = String());
	void remove_command(const String &p_name);
	bool has_command(const String &p_name) const;
	PackedStringArray get_commands() const;
	Variant run(const String &p_line);
	void write(const String &p_text);
	void clear();

	String get_text() const;

	// For the console's view.
	void write_line(const String &p_text, LineKind p_kind);
	const Vector<Line> &get_output() const { return output; }
	int get_output_serial() const { return output_serial; }
	const Vector<String> &get_history() const { return history; }
	PackedStringArray complete(const String &p_partial) const;
	Key get_open_key() const { return open_key; }

	Console();
	~Console();
};
