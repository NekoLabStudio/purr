#pragma once

// PurrOnline: the client for Purr's online services (online/ in Purr's repository). Every call
// returns a signal that fires once with the result, so a game can write
//   var result = await PurrOnline.read_save("slot1")
// The result is {"ok": bool, "status": HTTP status, "data": the answer, "error": a code like
// "version_conflict" or "" , "message": why}. The service's URL is the trill/url project
// setting, and the game's ID trill/game. Most games use Purr instead, which uses this.

#include "core/io/http_client.h"
#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/templates/hash_set.h"

#include "modules/websocket/websocket_peer.h"

// One call in flight: its completed signal is what PurrOnline's methods return.
class PurrOnlineCall : public RefCounted {
	GDCLASS(PurrOnlineCall, RefCounted);

protected:
	static void _bind_methods();
};

class PurrOnline : public Object {
	GDCLASS(PurrOnline, Object);

	static PurrOnline *singleton;

	String token;
	String account_id;
	HashSet<Ref<PurrOnlineCall>> pending;

	Ref<WebSocketPeer> lobby;
	Ref<WebSocketPeer> matchmaking;
	bool kicked_seen = false;
	// Why online play stopped for this session ("update_required", "version_unsupported",
	// "service_outdated" or "not_owned"), or empty.
	String blocked;
	Dictionary ownership_proof;
	// The Trill protocol this Purr needs (PROTOCOL in Trill's src/version.ts).
	static constexpr int PROTOCOL = 2;
	// The close code the service uses when the host removes a player (see Trill's src/lobby.ts).
	static constexpr int LOBBY_KICKED_CLOSE_CODE = 4001;

	Signal call(HTTPClient::Method p_method, const String &p_path, const Variant &p_body, const String &p_token, const Vector<String> &p_extra_headers = Vector<String>(), bool p_binary_answer = false);
	void finish(const Dictionary &p_response, const Ref<PurrOnlineCall> &p_call, bool p_binary_answer, bool p_signs_in);
	Error open_socket(Ref<WebSocketPeer> &r_socket, const String &p_path, const Callable &p_poll);
	void close_socket(Ref<WebSocketPeer> &r_socket, const Callable &p_poll);
	void send_lobby_message(const Dictionary &p_message);
	void poll_lobby();
	Vector<String> game_headers() const;
	void check_versions(const Dictionary &p_result);
	void poll_matchmaking();

protected:
	static void _bind_methods();

public:
	static PurrOnline *get_singleton() { return singleton; }

	bool is_configured() const;
	String get_blocked_reason() const { return blocked; }
	// This copy's proof of purchase (from PurrStore), sent with every sign-in.
	void set_ownership_proof(const Dictionary &p_proof) { ownership_proof = p_proof; }
	Signal get_ownership_challenge();
	Signal get_service_version();
	bool is_secure() const;
	String get_url() const;
	String get_game() const;

	String get_token() const { return token; }
	String get_account_id() const { return account_id; }
	void set_session(const String &p_token, const String &p_account_id);
	void sign_out();

	Signal sign_in_device(const String &p_secret, const String &p_display_name = String());
	Signal sign_in_steam(const String &p_ticket_hex, const String &p_display_name = String());
	Signal sign_in_game_center(const Dictionary &p_proof, const String &p_display_name = String());
	Signal sign_in_play_games(const String &p_server_auth_code, const String &p_display_name = String());

	Signal send_crash_report(const String &p_report);

	Signal get_account(const String &p_token = String());
	Signal set_display_name(const String &p_name);
	Signal delete_account();

	Signal list_saves(const String &p_token = String());
	Signal read_save(const String &p_slot, const String &p_token = String());
	Signal write_save(const String &p_slot, const PackedByteArray &p_data, int64_t p_replaces_version, const Dictionary &p_summary, const String &p_token = String());

	// Email: a code sent to the address, then verified to sign in, add the address to this
	// account (joining the address's account if it has one) or change it.
	Signal email_start(const String &p_email);
	// The challenge is what email_start() returned with the code.
	Signal email_verify(const String &p_email, const String &p_challenge, const String &p_code, const String &p_current_email = String(), const String &p_display_name = String());
	// On a host device: a guest's code (one that can't sign in), then their account from it, for
	// loading and saving this game's saves.
	Signal guest_email_start(const String &p_email);
	Signal guest_email_verify(const String &p_email, const String &p_challenge, const String &p_code, bool p_remember, const String &p_device_label, const String &p_display_name = String());

	Signal list_grants();
	Signal revoke_grant(const String &p_grant_id);
	Signal forget_grant(const String &p_grant_token);

	Signal create_lobby();
	Error join_lobby(const String &p_code);
	void send_signal(int64_t p_to_peer, const Variant &p_data);
	void leave_lobby();
	bool is_in_lobby() const { return lobby.is_valid(); }
	// In a lobby: the host removes a player; anyone reports or blocks a player they played with;
	// the host of a ranked lobby reports its result (peer IDs, first to last).
	void kick_peer(int64_t p_peer_id);
	void report_peer(int64_t p_peer_id, const String &p_reason, const String &p_details = String());
	void block_peer(int64_t p_peer_id);
	void report_result(const Array &p_ranking);
	// A check: a value every player's game can work out (usually a hash), compared by the lobby.
	void send_check(const String &p_name, const String &p_value);
	void renew_lobby_ice();
	// A flag: this player's game judged another's behaviour impossible.
	void flag_peer(int64_t p_peer_id, const String &p_reason);

	Error join_matchmaking(const String &p_mode, int p_players = 2);
	void leave_matchmaking();
	bool is_matchmaking() const { return matchmaking.is_valid(); }
	Signal get_rating(const String &p_mode);

	PurrOnline();
	~PurrOnline();
};
