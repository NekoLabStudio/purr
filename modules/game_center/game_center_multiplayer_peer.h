#pragma once

// GameCenterMultiplayerPeer: Godot's high-level multiplayer over a Game Center real-time match
// (GKMatch), found by matchmaking, by invites or by a party code. Every player is connected to
// every other through Apple's servers, so it's a mesh. Peer IDs come from the players'
// gamePlayerIDs, so every device works out the same ones without asking; the host, chosen with
// set_host(), is peer 1. send_message() carries text alongside the game's packets, for
// coordinating the match.

#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/list.h"
#include "scene/main/multiplayer_peer.h"

class GameCenterMultiplayerPeer : public MultiplayerPeer {
	GDCLASS(GameCenterMultiplayerPeer, MultiplayerPeer);

public:
	struct Packet {
		Vector<uint8_t> data;
		String from;
		int channel = 0;
		TransferMode mode = TRANSFER_MODE_RELIABLE;
		uint32_t sequence = 0;
	};

private:
	void *match = nullptr; // GKMatch, retained.
	void *delegate = nullptr; // Its delegate (and the invite screen's), retained.

	String host;
	HashSet<String> players; // Connected remote players, by gamePlayerID.
	HashSet<String> ignored; // Players this game no longer plays with.
	HashSet<String> to_announce; // Connected players the game hasn't heard about yet.

	Mutex mutex;
	List<Packet> arriving; // From GameKit's threads.
	List<Packet> packets; // Ready for the game.
	Packet current;
	HashMap<String, uint32_t> latest_sequence; // "<player> <channel>": unreliable ordered packets.
	uint32_t next_sequence = 0;

	int target_peer = 0;
	bool closed = false;
	bool host_left = false; // Until another host is set, the game has ended for this player.

	void take_match(void *p_match);
	void forget_match();
	Vector<String> targets() const;

protected:
	static void _bind_methods();

public:
	static constexpr int MAX_PACKET_SIZE = 87 * 1024; // GameKit's limit for reliable data.

	// For the delegate, on Godot's main thread.
	void match_found(void *p_match);
	void match_not_found(const String &p_error, const String &p_message);
	void player_connected(const String &p_player_id);
	void player_disconnected(const String &p_player_id);
	void message_arrived(const String &p_player_id, const String &p_message);
	// From any thread.
	void packet_arrived(const Packet &p_packet);

	Error find_match(int p_min_players, int p_max_players, int64_t p_player_group);
	Error find_party_match(const String &p_activity_id, const String &p_party_code);
	Error accept_invite();
	Error invite(int p_max_players);
	Error invite_player(const String &p_player_id, int p_max_players);
	void cancel_matchmaking();
	bool has_match() const { return match != nullptr; }

	void set_host(const String &p_player_id);
	String get_host() const { return host; }
	String get_local_player_id() const;
	PackedStringArray get_player_ids() const;
	int get_peer_id_for_player(const String &p_player_id) const;
	String get_player_for_peer_id(int p_peer_id) const;
	Error send_message(const String &p_message);
	static int peer_id_of(const String &p_player_id);

	// MultiplayerPeer.
	int get_available_packet_count() const override;
	Error get_packet(const uint8_t **r_buffer, int &r_buffer_size) override;
	Error put_packet(const uint8_t *p_buffer, int p_buffer_size) override;
	int get_max_packet_size() const override { return MAX_PACKET_SIZE; }
	void set_target_peer(int p_peer_id) override { target_peer = p_peer_id; }
	int get_packet_peer() const override;
	TransferMode get_packet_mode() const override;
	int get_packet_channel() const override;
	void disconnect_peer(int p_peer, bool p_force = false) override;
	bool is_server() const override;
	void poll() override;
	void close() override;
	int get_unique_id() const override;
	ConnectionStatus get_connection_status() const override;

	~GameCenterMultiplayerPeer();
};
