#pragma once

// GameCenter: Apple's Game Center on macOS, iOS and tvOS. Sign in (authenticate), the identity
// verification signature Purr's online services check, achievements, leaderboards (and their
// screens), invites and parties (game activities with a party code). Each asynchronous call
// returns a signal that fires once with a Dictionary: {"ok", "error", ...}.

#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/templates/hash_set.h"

// One call in flight; its completed signal is what GameCenter's methods return.
class GameCenterCall : public RefCounted {
	GDCLASS(GameCenterCall, RefCounted);

protected:
	static void _bind_methods();
};

class GameCenter : public Object {
	GDCLASS(GameCenter, Object);

public:
	enum Scope {
		SCOPE_GLOBAL, // The best scores.
		SCOPE_FRIENDS, // The best among the player's friends.
		SCOPE_AROUND_PLAYER, // Those around the player's own.
	};

private:

	static GameCenter *singleton;
	HashSet<Ref<GameCenterCall>> pending;
	bool authenticating = false;
	// GameKit calls the handler when it's first set and then only when the player's state changes:
	// set again, it doesn't ask again. So it's set once, and its last answer kept for later calls.
	bool handler_installed = false;
	Dictionary last_answer;
	Vector<Ref<GameCenterCall>> waiting_for_sign_in;

protected:
	static void _bind_methods();

public:
	static GameCenter *get_singleton() { return singleton; }

	// For the Objective-C side: hands a result to a call, on the main thread.
	Ref<GameCenterCall> begin_call();
	void finish_call(const Ref<GameCenterCall> &p_call, const Dictionary &p_result);
	void finish_sign_in(const Dictionary &p_result);

	Signal authenticate();
	bool is_authenticated() const;
	String get_player_name() const;
	String get_team_player_id() const;
	Signal fetch_identity_verification();
	Signal report_achievement(const String &p_id, double p_percent = 100.0);
	Signal submit_score(const String &p_leaderboard_id, int64_t p_score);
	Signal get_achievements();
	Signal get_scores(const String &p_leaderboard_id, Scope p_scope = SCOPE_GLOBAL, int p_count = 10);
	void show_achievements();
	void show_leaderboard(const String &p_leaderboard_id = String());
	void invite_players(int64_t p_player_group, int p_max_players);
	void start_party(const String &p_activity_id, const String &p_party_code);
	void end_party();

	GameCenter();
	~GameCenter();
};

VARIANT_ENUM_CAST(GameCenter::Scope);
