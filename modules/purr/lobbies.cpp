#include "lobbies.h"

#include "purr.h"

#include "core/object/class_db.h"

Lobbies *Lobbies::singleton = nullptr;

bool Lobbies::is_available() const {
	return Purr::get_singleton()->is_available("lobbies");
}

bool Lobbies::has_crossplay() const {
	return Purr::get_singleton()->is_available("crossplay");
}

bool Lobbies::can_invite() const {
	return Purr::get_singleton()->is_available("invites");
}

bool Lobbies::can_matchmake() const {
	return Purr::get_singleton()->is_available("matchmaking");
}

Signal Lobbies::create() {
	return Purr::get_singleton()->lobby_create();
}

Signal Lobbies::join(const String &p_code) {
	return Purr::get_singleton()->lobby_join(p_code);
}

void Lobbies::leave() {
	Purr::get_singleton()->lobby_leave();
}

void Lobbies::kick(int64_t p_peer_id) {
	Purr::get_singleton()->lobby_kick(p_peer_id);
}

void Lobbies::invite_friends() {
	Purr::get_singleton()->invite_friends();
}

Signal Lobbies::matchmake(const String &p_mode, int p_players) {
	return Purr::get_singleton()->matchmake(p_mode, p_players);
}

void Lobbies::cancel_matchmaking() {
	Purr::get_singleton()->matchmake_cancel();
}

void Lobbies::report_result(const Array &p_ranking) {
	Purr::get_singleton()->report_result(p_ranking);
}

Signal Lobbies::get_rating(const String &p_mode) {
	return Purr::get_singleton()->get_rating(p_mode);
}

void Lobbies::check(const String &p_name, const Variant &p_value) {
	Purr::get_singleton()->check(p_name, p_value);
}

void Lobbies::flag(int64_t p_peer_id, const String &p_reason) {
	Purr::get_singleton()->flag(p_peer_id, p_reason);
}

void Lobbies::report(int64_t p_peer_id, ReportReason p_reason, const String &p_details) {
	static const char *reasons[] = { "cheating", "abuse", "name", "other" };
	Purr::get_singleton()->report_player(p_peer_id, reasons[CLAMP((int)p_reason, 0, 3)], p_details);
}

void Lobbies::block(int64_t p_peer_id) {
	Purr::get_singleton()->block_player(p_peer_id);
}

void Lobbies::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_available"), &Lobbies::is_available);
	ClassDB::bind_method(D_METHOD("has_crossplay"), &Lobbies::has_crossplay);
	ClassDB::bind_method(D_METHOD("can_invite"), &Lobbies::can_invite);
	ClassDB::bind_method(D_METHOD("can_matchmake"), &Lobbies::can_matchmake);
	ClassDB::bind_method(D_METHOD("create"), &Lobbies::create);
	ClassDB::bind_method(D_METHOD("join", "code"), &Lobbies::join);
	ClassDB::bind_method(D_METHOD("leave"), &Lobbies::leave);
	ClassDB::bind_method(D_METHOD("kick", "peer_id"), &Lobbies::kick);
	ClassDB::bind_method(D_METHOD("invite_friends"), &Lobbies::invite_friends);
	ClassDB::bind_method(D_METHOD("matchmake", "mode", "players"), &Lobbies::matchmake, DEFVAL(2));
	ClassDB::bind_method(D_METHOD("cancel_matchmaking"), &Lobbies::cancel_matchmaking);
	ClassDB::bind_method(D_METHOD("report_result", "ranking"), &Lobbies::report_result);
	ClassDB::bind_method(D_METHOD("get_rating", "mode"), &Lobbies::get_rating);
	ClassDB::bind_method(D_METHOD("check", "name", "value"), &Lobbies::check);
	ClassDB::bind_method(D_METHOD("flag", "peer_id", "reason"), &Lobbies::flag);
	ClassDB::bind_method(D_METHOD("report", "peer_id", "reason", "details"), &Lobbies::report, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("block", "peer_id"), &Lobbies::block);

	ADD_SIGNAL(MethodInfo("joined", PropertyInfo(Variant::STRING, "code")));
	ADD_SIGNAL(MethodInfo("kicked"));
	ADD_SIGNAL(MethodInfo("host_changed", PropertyInfo(Variant::BOOL, "is_host")));
	ADD_SIGNAL(MethodInfo("result_recorded"));
	ADD_SIGNAL(MethodInfo("player_flagged", PropertyInfo(Variant::INT, "peer_id"), PropertyInfo(Variant::STRING, "reason")));
	ADD_SIGNAL(MethodInfo("check_completed", PropertyInfo(Variant::STRING, "name"), PropertyInfo(Variant::BOOL, "agreed"), PropertyInfo(Variant::ARRAY, "disagreeing")));

	BIND_ENUM_CONSTANT(REPORT_CHEATING);
	BIND_ENUM_CONSTANT(REPORT_ABUSE);
	BIND_ENUM_CONSTANT(REPORT_NAME);
	BIND_ENUM_CONSTANT(REPORT_OTHER);
}

Lobbies::Lobbies() {
	singleton = this;
}

Lobbies::~Lobbies() {
	singleton = nullptr;
}
