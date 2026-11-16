#include "local_players.h"

#include "purr.h"

#include "core/object/class_db.h"

LocalPlayers *LocalPlayers::singleton = nullptr;

Ref<LocalPlayer> LocalPlayers::get_player() const {
	return Purr::get_singleton()->get_player();
}

Array LocalPlayers::get_players() const {
	return Purr::get_singleton()->get_local_players();
}

Array LocalPlayers::get_remembered() const {
	return Purr::get_singleton()->get_remembered_guests();
}

Ref<LocalPlayer> LocalPlayers::add_guest(const Ref<LocalPlayer> &p_remembered) {
	return Purr::get_singleton()->add_guest(p_remembered);
}

void LocalPlayers::remove_guest(const Ref<LocalPlayer> &p_guest) {
	Purr::get_singleton()->remove_guest(p_guest);
}

bool LocalPlayers::can_invite_guests() const {
	return Purr::get_singleton()->is_available("guests");
}

Signal LocalPlayers::invite_guest(const String &p_email) {
	return Purr::get_singleton()->guest_email_start(p_email);
}

Signal LocalPlayers::confirm_guest(const String &p_code, bool p_remember, const String &p_name) {
	return Purr::get_singleton()->guest_email_finish(p_code, p_remember, p_name);
}

void LocalPlayers::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_player"), &LocalPlayers::get_player);
	ClassDB::bind_method(D_METHOD("get_players"), &LocalPlayers::get_players);
	ClassDB::bind_method(D_METHOD("get_remembered"), &LocalPlayers::get_remembered);
	ClassDB::bind_method(D_METHOD("add_guest", "remembered"), &LocalPlayers::add_guest, DEFVAL(Ref<LocalPlayer>()));
	ClassDB::bind_method(D_METHOD("remove_guest", "guest"), &LocalPlayers::remove_guest);
	ClassDB::bind_method(D_METHOD("can_invite_guests"), &LocalPlayers::can_invite_guests);
	ClassDB::bind_method(D_METHOD("invite_guest", "email"), &LocalPlayers::invite_guest);
	ClassDB::bind_method(D_METHOD("confirm_guest", "code", "remember", "name"), &LocalPlayers::confirm_guest, DEFVAL(false), DEFVAL(String()));

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "player", PROPERTY_HINT_RESOURCE_TYPE, "LocalPlayer", PROPERTY_USAGE_NONE), "", "get_player");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "players", PROPERTY_HINT_ARRAY_TYPE, "LocalPlayer", PROPERTY_USAGE_NONE), "", "get_players");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "remembered", PROPERTY_HINT_ARRAY_TYPE, "LocalPlayer", PROPERTY_USAGE_NONE), "", "get_remembered");

	ADD_SIGNAL(MethodInfo("guest_added", PropertyInfo(Variant::OBJECT, "guest", PROPERTY_HINT_RESOURCE_TYPE, "LocalPlayer")));
}

LocalPlayers::LocalPlayers() {
	singleton = this;
}

LocalPlayers::~LocalPlayers() {
	singleton = nullptr;
}
