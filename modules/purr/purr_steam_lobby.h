#pragma once

// A Steam lobby, for Steam players only, when the game doesn't use Purr's online services. The
// code is the Steam lobby's ID and the game plays over Steam's networking (GodotSteam's
// SteamMultiplayerPeer, a mesh with the lobby's owner as peer 1). Purr's own messages (removals,
// checks, flags, results, ratings) go as lobby chat messages starting "purr:", which Steam
// stamps with their sender. With lobbies/host_migration on, the lobby's new owner (Steam picks
// one when the owner leaves) becomes peer 1 and the others reconnect to them, keeping their peer
// IDs. Matchmaking searches Steam's public lobbies for one of the same mode and a similar rating,
// widening the range and the distance as the player waits, and opens its own when there's none.

#include "purr_platform_lobby.h"

#include "core/templates/hash_set.h"
#include "scene/main/multiplayer_peer.h"

class PurrSteamLobby : public PurrPlatformLobby {
	GDCLASS(PurrSteamLobby, PurrPlatformLobby);

	uint64_t lobby_id = 0;
	uint64_t owner = 0;
	Ref<MultiplayerPeer> peer;
	bool listening = false;
	// Steam answers every createLobby() and joinLobby() with the same signals, the game's own
	// included: these say which answers are Purr's, and which are for lobbies it no longer wants
	// (left before Steam answered), to leave at once.
	int creates_pending = 0;
	int creates_abandoned = 0;
	uint64_t joining_id = 0;
	HashSet<uint64_t> joins_abandoned;

	String matchmaking_mode;
	int matchmaking_players = 0;
	uint64_t matchmaking_started = 0;
	uint64_t generation = 0; // Which search a timer or a lobby list is for.
	bool own_matchmaking_lobby = false;

	Object *steam() const;
	void listen_for_lobbies();
	void create_steam_lobby(int64_t p_type, int p_max_members);
	void join_steam_lobby(uint64_t p_lobby_id);
	int64_t constant(const char *p_name) const;
	uint64_t own_id() const;
	PackedStringArray lobby_members() const;
	void listen(bool p_listen);
	void start_session();
	void start_peer(bool p_host, int64_t p_peer_id);
	void owner_left();

	void lobby_created(int64_t p_connect, int64_t p_lobby_id);
	void lobby_joined(int64_t p_lobby_id, int64_t p_permissions, bool p_locked, int64_t p_response);
	void chat_updated(int64_t p_lobby_id, int64_t p_changed_id, int64_t p_making_change_id, int64_t p_state);
	void message_received(int64_t p_lobby_id, int64_t p_user, const String &p_message, int64_t p_type);
	void peer_connected(int64_t p_peer_id);

	int search_level() const;
	void search();
	void search_later(uint64_t p_generation);
	void search_again(uint64_t p_generation);
	void lobbies_found(const Array &p_lobbies, uint64_t p_generation);
	void matchmaking_lobby_entered();
	void begin_match();
	void resume_matchmaking();

protected:
	static void _bind_methods() {}

	String _platform() const override { return "steam"; }
	String _own_member() const override;
	void _send(const Dictionary &p_message) override;
	void _drop(const String &p_member) override;
	void _close() override;
	void _report(const String &p_member) override;

public:
	void create(const Ref<RefCounted> &p_call) override;
	void join(const String &p_code, const Ref<RefCounted> &p_call) override;
	void matchmake(const String &p_mode, int p_players, const Ref<RefCounted> &p_call) override;
	void cancel_matchmaking() override;
	bool is_matchmaking() const override { return !matchmaking_mode.is_empty(); }
	uint64_t get_lobby_id() const { return lobby_id; }

	~PurrSteamLobby();
};
