#pragma once

// A Project Settings tab listing a game's achievements, leaderboards, stats or store items by their own
// IDs, with each one's name on every platform: <prefix>/<id>/<platform> in the project's settings.
// A column can instead be a choice (a leaderboard's order) or have a tooltip of its own.

#include "core/templates/hash_map.h"
#include "scene/gui/box_container.h"

class LineEdit;
class Tree;

class PlatformNamesSettings : public VBoxContainer {
	GDCLASS(PlatformNamesSettings, VBoxContainer);

	String prefix;
	String empty_tooltip; // What an empty name means.
	Vector<String> columns; // The setting under each ID, one column each.
	// Columns that are a choice: each value with its label, the first the default.
	HashMap<String, Vector<Pair<String, String>>> choices;
	HashMap<String, String> tooltips; // Columns' own tooltips, in place of empty_tooltip.
	LineEdit *id_edit = nullptr;
	Tree *tree = nullptr;
	bool updating = false;

	Vector<String> ids() const;
	void add_pressed();
	void item_edited();
	void button_clicked(Object *p_item, int p_column, int p_id, MouseButton p_button);
	void changed();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void update_list();
	void set_column_choices(const String &p_column, const Vector<Pair<String, String>> &p_choices);
	void set_column_tooltip(const String &p_column, const String &p_tooltip);

	// p_prefix: "achievements", "leaderboards", "stats" or "store"; p_columns: the setting under each ID with
	// its column's title, in order; p_empty_tooltip: what leaving a name empty means.
	PlatformNamesSettings(const String &p_prefix, const Vector<Pair<String, String>> &p_columns, const String &p_empty_tooltip, const String &p_id_placeholder);
};
