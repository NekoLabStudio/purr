#include "saves.h"

#include "purr.h"

#include "core/object/class_db.h"

Saves *Saves::singleton = nullptr;

Ref<PlayerSaves> Saves::player_saves() const {
	const Ref<LocalPlayer> player = Purr::get_singleton()->get_player();
	return player.is_valid() ? player->get_saves() : Ref<PlayerSaves>();
}

void Saves::save(const String &p_slot, const Variant &p_data, const Dictionary &p_summary) {
	const Ref<PlayerSaves> saves = player_saves();
	ERR_FAIL_COND_MSG(saves.is_null(), "Sign in (Account.sign_in) before saving.");
	saves->save(p_slot, p_data, p_summary);
}

Variant Saves::load(const String &p_slot) const {
	const Ref<PlayerSaves> saves = player_saves();
	ERR_FAIL_COND_V_MSG(saves.is_null(), Variant(), "Sign in (Account.sign_in) before loading.");
	return saves->load(p_slot);
}

bool Saves::has(const String &p_slot) const {
	const Ref<PlayerSaves> saves = player_saves();
	return saves.is_valid() && saves->has(p_slot);
}

PackedStringArray Saves::get_slots() const {
	const Ref<PlayerSaves> saves = player_saves();
	return saves.is_valid() ? saves->get_game_slots() : PackedStringArray();
}

Dictionary Saves::get_summary(const String &p_slot) const {
	const Ref<PlayerSaves> saves = player_saves();
	return saves.is_valid() ? saves->get_summary(p_slot) : Dictionary();
}

void Saves::resolve_conflict(const String &p_slot, bool p_keep_local) {
	const Ref<PlayerSaves> saves = player_saves();
	ERR_FAIL_COND(saves.is_null());
	saves->resolve_conflict(p_slot, p_keep_local);
}

void Saves::sync() {
	const Ref<PlayerSaves> saves = player_saves();
	if (saves.is_valid()) {
		saves->sync();
	}
}

PlayerSaves::Backend Saves::get_backend() const {
	const Ref<PlayerSaves> saves = player_saves();
	return saves.is_valid() ? saves->get_backend() : PlayerSaves::BACKEND_LOCAL;
}

void Saves::_bind_methods() {
	ClassDB::bind_method(D_METHOD("save", "slot", "data", "summary"), &Saves::save, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("load", "slot"), &Saves::load);
	ClassDB::bind_method(D_METHOD("has", "slot"), &Saves::has);
	ClassDB::bind_method(D_METHOD("get_slots"), &Saves::get_slots);
	ClassDB::bind_method(D_METHOD("get_summary", "slot"), &Saves::get_summary);
	ClassDB::bind_method(D_METHOD("resolve_conflict", "slot", "keep_local"), &Saves::resolve_conflict);
	ClassDB::bind_method(D_METHOD("sync"), &Saves::sync);
	ClassDB::bind_method(D_METHOD("get_backend"), &Saves::get_backend);

	ADD_SIGNAL(MethodInfo("synced", PropertyInfo(Variant::STRING, "slot")));
	ADD_SIGNAL(MethodInfo("conflict", PropertyInfo(Variant::STRING, "slot"), PropertyInfo(Variant::DICTIONARY, "local_summary"), PropertyInfo(Variant::DICTIONARY, "remote_summary")));
}

Saves::Saves() {
	singleton = this;
}

Saves::~Saves() {
	singleton = nullptr;
}
