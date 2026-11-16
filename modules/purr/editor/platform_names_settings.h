#pragma once

// A Project Settings tab listing a game's achievements or leaderboards by their own IDs, with each
// one's name on every platform: <prefix>/<id>/<platform> in the project's settings, an empty name
// meaning the platform uses the same ID.

#include "scene/gui/box_container.h"

class LineEdit;
class Tree;

class PlatformNamesSettings : public VBoxContainer {
	GDCLASS(PlatformNamesSettings, VBoxContainer);

	String prefix;
	Vector<String> columns; // The setting under each ID, one column each.
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

	// p_prefix: "achievements" or "leaderboards"; p_columns: the setting under each ID with its
	// column's title, in order.
	PlatformNamesSettings(const String &p_prefix, const Vector<Pair<String, String>> &p_columns);
};
