#pragma once

// PlayGames: Google Play Games Services on Android. Sign in, the server auth code Purr's online
// services exchange with Google, achievements, leaderboards and saved games. Each asynchronous
// call returns a signal that fires once with a Dictionary: {"ok", "error", ...}.
//
// Off unless the game sets its Play Games application ID in play_games/app_id. The Android
// side is a Godot Android plugin (android/) that Purr's export plugin adds to Gradle exports.

#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"

// One call in flight; its completed signal is what PlayGames' methods return.
class PlayGamesCall : public RefCounted {
	GDCLASS(PlayGamesCall, RefCounted);

protected:
	static void _bind_methods();
};

class PlayGames : public Object {
	GDCLASS(PlayGames, Object);

public:
	enum Scope {
		SCOPE_GLOBAL, // The best scores.
		SCOPE_FRIENDS, // The best among the player's friends.
		SCOPE_AROUND_PLAYER, // Those around the player's own.
	};

private:

	static PlayGames *singleton;
	int next_call_id = 1;
	HashMap<int, Ref<PlayGamesCall>> pending;
	String player_id;
	String player_name;

	// The Java plugin (PlayGamesAndroid), once Godot has registered it.
	Object *android() const;
	bool connect_android();
	int begin_call(Ref<PlayGamesCall> &r_call);
	Signal fail_now(const String &p_error);
	void finished(int p_call_id, const Dictionary &p_result);
	void signed_in(const Dictionary &p_result, const Ref<PlayGamesCall> &p_call);
	void file_listed(const Dictionary &p_result, const Ref<PlayGamesCall> &p_call);
	void file_loaded(const Dictionary &p_result, const Ref<PlayGamesCall> &p_call);

protected:
	static void _bind_methods();

public:
	static PlayGames *get_singleton() { return singleton; }

	bool is_available() const;
	Signal authenticate(bool p_interactive = true);
	bool is_authenticated() const;
	String get_player_name() const { return player_name; }
	String get_player_id() const { return player_id; }
	Signal request_server_auth_code();

	void unlock_achievement(const String &p_id);
	void increment_achievement(const String &p_id, int p_steps);
	void set_achievement_steps(const String &p_id, int p_steps);
	Signal get_achievements();
	Signal get_scores(const String &p_leaderboard_id, Scope p_scope = SCOPE_GLOBAL, int p_count = 10);
	void submit_score(const String &p_leaderboard_id, int64_t p_score);
	void show_achievements();
	void show_leaderboard(const String &p_leaderboard_id = String());

	// Saved games, the same calls as ICloud's, so Purr's saves use either the same way.
	bool is_saved_games_enabled() const;
	Signal save_file(const String &p_name, const PackedByteArray &p_data, const Dictionary &p_metadata = Dictionary());
	Signal load_file(const String &p_name);
	Signal list_files();
	Signal delete_file(const String &p_name);

	PlayGames();
	~PlayGames();
};

VARIANT_ENUM_CAST(PlayGames::Scope);
