#pragma once

// Friends, for Purr: who's playing, the lobbies they're in, and invites. On Steam, the lobby a
// player is in goes in their rich presence (which also gives friends Steam's own "Join Game"), and
// so do the friends they invited to it. On Game Center, invites go through Apple's invite screen
// and parties (game_center/party_activity); either carries the lobby's code, encoded, and
// whoever accepts joins it. Game Center and Play Games also give the player's friends list,
// asked for (with the player's consent) the first time the game reads it.

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"

// What Steam starts the game with (on its command line, or in join_game_requested) when a friend
// joins or accepts an invite: a crossplay lobby's code, or a Steam lobby's ID.
inline constexpr const char *PURR_INVITE_ARGUMENT = "+purr_lobby";
inline constexpr const char *STEAM_LOBBY_ARGUMENT = "+connect_lobby";

class PurrFriends : public RefCounted {
	GDCLASS(PurrFriends, RefCounted);

	String lobby_code;
	bool crossplay = false; // A crossplay lobby's code, or a Steam lobby's ID.
	PackedStringArray invited; // Friends invited to this lobby, newest last.
	HashMap<String, String> lobby_invites; // Steam lobby invites received: inviter -> lobby.
	bool started = false;
	bool changed_queued = false;
	Array platform_friends; // Game Center's or Play Games' friends, once loaded.
	Array service_ids; // The same friends as the online services know them: [{"provider", "id"}].
	bool platform_friends_asked = false;

	void steam_friends_changed(int64_t p_steam_id, int64_t p_detail);
	void steam_lobby_invited(int64_t p_inviter, int64_t p_lobby, int64_t p_game);
	void game_center_invite_accepted(int64_t p_player_group);
	void game_center_party_joined(const String &p_party_code);
	void write_presence();
	void ask_platform_friends();
	void platform_friends_loaded(const Dictionary &p_result);
	void changed();
	void emit_changed();

protected:
	static void _bind_methods() {}

public:
	// Lobby codes as a number and back (0 means none), and as a Game Center party code ("ABCD-EFGH",
	// in GameKit's party code alphabet) and back; empty when it isn't one.
	static int64_t code_to_number(const String &p_code);
	static String number_to_code(int64_t p_number);
	static String code_to_party_code(const String &p_code);
	static String party_code_to_code(const String &p_party_code);

	// Listens for friends and invites, from signing in on.
	void start();
	// The lobby the player is in now, or empty: shown to friends (and the party ended on leaving).
	void set_lobby(const String &p_code, bool p_crossplay);

	bool is_available() const;
	// Asks Game Center or Play Games the first time; Friends.changed fires when they answer.
	Array get_friends();
	// The player's platform friends as the online services know players ({"provider", "id"}), for
	// their leaderboards' friends scope. The service compares only keyed hashes of them.
	Array get_service_ids();
	void invite(const String &p_friend_id);
	void invite_with_platform_screen();
	bool can_invite() const;
};
