#pragma once

// A Game Center match as a lobby, for Apple players when the game doesn't use Purr's online
// services, over GameCenterMultiplayerPeer. The player who creates the lobby hosts it. With
// game_center/party_activity set (on OS 26 and later), its code is a Game Center party code that
// others join with, and friends see the party in Apple's Games app; otherwise it has no code and
// friends join through invites. Whoever accepts an invite plays in the inviter's lobby. Purr's
// messages go alongside the game's packets (GameCenterMultiplayerPeer.send_message), and the host
// says it's the host when each player connects. Matchmaking asks Game Center for players of the
// same mode and a similar rating (a player group for each mode, size and rating band), widening
// the band as the player waits; the player with the lowest ID hosts the match. With
// lobbies/host_migration on, the remaining player with the lowest ID takes over.

#include "purr_platform_lobby.h"

#include "scene/main/multiplayer_peer.h"

class PurrGameCenterLobby : public PurrPlatformLobby {
	GDCLASS(PurrGameCenterLobby, PurrPlatformLobby);

	Ref<MultiplayerPeer> peer; // GameCenterMultiplayerPeer.
	String joining_code; // Waiting for the host, to complete join() with this code.
	bool joining = false;

	String matchmaking_mode;
	int matchmaking_players = 0;
	uint64_t matchmaking_started = 0;
	uint64_t generation = 0; // Which wait a timer is for.

	void new_peer();
	void set_tree_peer();
	String host() const;
	void match_started();
	void match_failed(const String &p_error, const String &p_message);
	void player_connected(const String &p_player_id);
	void player_disconnected(const String &p_player_id);
	void message_received(const String &p_player_id, const String &p_message);
	void ready_if_host_here();
	void timed_out(uint64_t p_generation);
	void after(double p_seconds, uint64_t p_generation);

	int search_level() const;
	void find_match();
	void widen(uint64_t p_generation);
	void begin_match();

protected:
	static void _bind_methods() {}

	String _platform() const override { return "game_center"; }
	String _own_member() const override;
	void _send(const Dictionary &p_message) override;
	void _drop(const String &p_member) override;
	void _close() override;
	void _report(const String &p_member) override;

public:
	static bool is_supported();
	static bool has_parties();

	void create(const Ref<RefCounted> &p_call) override;
	void join(const String &p_code, const Ref<RefCounted> &p_call) override;
	void matchmake(const String &p_mode, int p_players, const Ref<RefCounted> &p_call) override;
	void cancel_matchmaking() override;
	bool is_matchmaking() const override { return !matchmaking_mode.is_empty(); }

	// An invite to another player's match, accepted in Game Center: joins their lobby.
	void join_invite(const String &p_sender_id);
	void invite_with_platform_screen();
	void invite(const String &p_friend_id);
};
