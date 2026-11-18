#pragma once

// Options: the player's settings, as an options menu sets them. Each has a key ("audio/music_volume",
// "game/show_hints") and a default; only those the player changed are saved, in user://options.cfg,
// so a game update that changes a default reaches every player who never touched it. The engine's
// own (audio buses' volumes, the window, vsync, the frame rate cap) take effect as they change and
// as the game starts. The game's come from Project Settings, under options/, or from define().

#include "core/object/object.h"
#include "core/templates/hash_map.h"
#include "core/variant/dictionary.h"

class Options : public Object {
	GDCLASS(Options, Object);

	struct Option {
		Variant default_value;
		Dictionary info; // min and max, or choices.
	};

	static Options *singleton;

	HashMap<String, Option> options;
	Vector<String> order; // Keys in the order defined.
	Dictionary saved; // What the file has, by key: changed values, and keys no option has now.
	bool audio_defined = false;
	bool apply_graphics_tier = true;
	bool dirty = false;
	bool flushing = false;
	uint64_t last_write_usec = 0;

	static String _path();
	static Dictionary _read_file();
	void _start();
	void _define_built_ins();
	void _define_from_project();
	void _ensure_audio();
	bool _conform(const String &p_key, const Variant &p_value, Variant &r_value) const;
	void _apply(const String &p_key);
	void _apply_graphics_tier(bool p_starting);
	void _queue_write();
	void _flush();
	void _write();

protected:
	static void _bind_methods();

public:
	static Options *get_singleton() { return singleton; }
	static void apply_before_window();

	void define(const String &p_key, const Variant &p_default, const Dictionary &p_info = Dictionary());
	bool has_option(const String &p_key);
	PackedStringArray get_options();
	Variant get_value(const String &p_key);
	void set_value(const String &p_key, const Variant &p_value);
	Variant get_default(const String &p_key);
	Dictionary get_info(const String &p_key);
	void reset(const String &p_key);
	void reset_all();

	void finish();
	void window_mode_check();

	Options();
	~Options();
};
