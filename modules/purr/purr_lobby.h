#pragma once

// A crossplay lobby: Purr's online service introduces the players and relays WebRTC signalling,
// then the game plays peer to peer (or through TURN) with a WebRTCMultiplayerPeer set on the scene
// tree. The host is peer 1 and every other player connects to the host, as with Godot's server
// and clients; the lobby stays open for players joining later. Matchmaking finds a ranked lobby
// the same way, by code. With lobbies/host_migration on, when the host leaves, the player
// who's been there longest becomes peer 1 and the others reconnect to them.

#include "modules/webrtc/webrtc_multiplayer_peer.h"

#include "core/object/ref_counted.h"

class PurrLobby : public RefCounted {
	GDCLASS(PurrLobby, RefCounted);

	String code;
	int64_t peer_id = 0;
	Array ice_servers;
	Ref<WebRTCMultiplayerPeer> peer;
	Ref<RefCounted> pending_call; // Completed once the lobby has welcomed the player.

	String matchmaking_mode;
	bool matchmade = false;

	// Completes the waiting call with this failure and leaves.
	void fail(const String &p_error, const String &p_message);
	void matched(const String &p_code);
	void matchmaking_closed();
	Error start_peer();
	void disconnect_matchmaking();
	void lobby_created(const Dictionary &p_result, const Ref<RefCounted> &p_call);
	void enter(const String &p_code, const Ref<RefCounted> &p_call);
	void connect_signals();
	void disconnect_signals();

	Ref<WebRTCPeerConnection> connection_to(int64_t p_peer_id);
	void welcomed(int64_t p_peer_id, const Array &p_members, const Array &p_ice_servers);
	void peer_joined(int64_t p_peer_id, const String &p_display_name);
	void peer_left(int64_t p_peer_id);
	void signal_received(int64_t p_from_peer, const Variant &p_data);
	void closed();
	void host_changed(int64_t p_old_peer_id, bool p_you);
	void kicked();
	void result_recorded();
	void check_result(const String &p_name, bool p_agreed, const Array &p_disagreeing);
	void flagged(int64_t p_peer_id, const String &p_reason);

	void description_created(const String &p_type, const String &p_sdp, int64_t p_to_peer);
	void candidate_created(const String &p_media, int64_t p_index, const String &p_name, int64_t p_to_peer);

protected:
	static void _bind_methods() {}

public:
	// Both complete the call with {"ok", "code"} or {"ok": false, "error", "message"}.
	void create(const Ref<RefCounted> &p_call);
	void join(const String &p_code, const Ref<RefCounted> &p_call);
	// Waits for players of a similar rating in this ranked mode, then joins the lobby they're
	// given; completes the call like join().
	void matchmake(const String &p_mode, int p_players, const Ref<RefCounted> &p_call);
	void leave();
	void cancel_matchmaking() { fail("cancelled", "Matchmaking was cancelled."); }

	bool is_active() const { return !code.is_empty(); }
	bool is_matchmaking() const { return !matchmaking_mode.is_empty(); }
	bool is_host() const { return peer_id == 1; }
	String get_code() const { return code; }

	~PurrLobby();
};
