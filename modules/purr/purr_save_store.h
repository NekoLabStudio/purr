#pragma once

// Where a player's saves live on this device: files in user://purr/saves/<owner>/, or, on tvOS,
// for saves with no copy anywhere else, the app's preferences (UserDefaults). tvOS deletes apps'
// files when it needs space; its preferences, about 500 KB per app, are the one local store it
// keeps.

#include "core/string/ustring.h"
#include "core/variant/variant.h"

class PurrSaveStore {
	String owner;
	bool preferences = false;

	String folder() const;
	Dictionary read_preferences() const;
	void write_preferences(const Dictionary &p_files) const;

public:
	void setup(const String &p_owner, bool p_no_other_copy);

	PackedByteArray read(const String &p_name) const;
	bool write(const String &p_name, const PackedByteArray &p_data) const;
	bool exists(const String &p_name) const;
	void remove(const String &p_name) const;
	PackedStringArray list() const;
	void remove_all() const;
};
