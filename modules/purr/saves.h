#pragma once

// Saves: player 1's saves, local first: written to disk at once and synced in the background with
// the online service, the platform's cloud or nothing (saves/backend in Project Settings). A
// guest's are their LocalPlayer's saves.

#include "player_saves.h"

#include "core/object/object.h"

class Saves : public Object {
	GDCLASS(Saves, Object);

	static Saves *singleton;
	Ref<PlayerSaves> player_saves() const;

protected:
	static void _bind_methods();

public:
	static Saves *get_singleton() { return singleton; }

	void save(const String &p_slot, const Variant &p_data, const Dictionary &p_summary = Dictionary());
	Variant load(const String &p_slot) const;
	bool has(const String &p_slot) const;
	PackedStringArray get_slots() const;
	Dictionary get_summary(const String &p_slot) const;
	void resolve_conflict(const String &p_slot, bool p_keep_local);
	void sync();
	PlayerSaves::Backend get_backend() const;

	Saves();
	~Saves();
};
