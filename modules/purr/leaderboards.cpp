#include "leaderboards.h"

#include "purr.h"

#include "core/object/class_db.h"

Leaderboards *Leaderboards::singleton = nullptr;

bool Leaderboards::is_available() const {
	return Purr::get_singleton()->is_available("leaderboards");
}

void Leaderboards::submit_score(const String &p_id, int64_t p_score) {
	Purr::get_singleton()->submit_score(p_id, p_score);
}

Signal Leaderboards::get_scores(const String &p_id, Scope p_scope, int p_count) {
	static const char *scopes[] = { "global", "friends", "around_player" };
	return Purr::get_singleton()->get_scores(p_id, scopes[CLAMP((int)p_scope, 0, 2)], p_count);
}

void Leaderboards::show(const String &p_id) {
	Purr::get_singleton()->show_leaderboard(p_id);
}

void Leaderboards::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_available"), &Leaderboards::is_available);
	ClassDB::bind_method(D_METHOD("submit_score", "id", "score"), &Leaderboards::submit_score);
	ClassDB::bind_method(D_METHOD("get_scores", "id", "scope", "count"), &Leaderboards::get_scores, DEFVAL(SCOPE_GLOBAL), DEFVAL(10));
	ClassDB::bind_method(D_METHOD("show", "id"), &Leaderboards::show, DEFVAL(String()));

	BIND_ENUM_CONSTANT(SCOPE_GLOBAL);
	BIND_ENUM_CONSTANT(SCOPE_FRIENDS);
	BIND_ENUM_CONSTANT(SCOPE_AROUND_PLAYER);
}

Leaderboards::Leaderboards() {
	singleton = this;
}

Leaderboards::~Leaderboards() {
	singleton = nullptr;
}
