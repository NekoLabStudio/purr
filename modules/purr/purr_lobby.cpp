#include "purr_lobby.h"

#include "lobbies.h"
#include "purr.h"

#include "modules/purr_online/purr_online.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "scene/main/multiplayer_api.h"
#include "scene/main/scene_tree.h"

namespace {

Dictionary failure(const String &p_error, const String &p_message) {
	Dictionary result;
	result["ok"] = false;
	result["error"] = p_error;
	result["message"] = p_message;
	return result;
}

PurrOnline *online() {
	return PurrOnline::get_singleton();
}

} // namespace

void PurrLobby::create(const Ref<RefCounted> &p_call) {
	leave();
	Signal received = online()->get_ice_servers();
	received.connect(callable_mp(this, &PurrLobby::ice_servers_received).bind(p_call, true, String()), CONNECT_ONE_SHOT);
}

void PurrLobby::join(const String &p_code, const Ref<RefCounted> &p_call) {
	leave();
	Signal received = online()->get_ice_servers();
	received.connect(callable_mp(this, &PurrLobby::ice_servers_received).bind(p_call, false, p_code.strip_edges()), CONNECT_ONE_SHOT);
}

void PurrLobby::ice_servers_received(const Dictionary &p_result, const Ref<RefCounted> &p_call, bool p_create, const String &p_code) {
	if (!p_result.get("ok", false)) {
		purr_complete(p_call, failure("unavailable", "Couldn't reach the online service."));
		return;
	}
	ice_servers = Dictionary(p_result["data"]).get("ice_servers", Array());
	if (!matchmaking_mode.is_empty()) {
		if (online()->join_matchmaking(matchmaking_mode, matchmaking_players) != OK) {
			matchmaking_mode = String();
			purr_complete(p_call, failure("matchmaking_failed", "Couldn't reach matchmaking."));
			return;
		}
		pending_call = p_call;
		online()->connect(SNAME("matchmaking_matched"), callable_mp(this, &PurrLobby::matched));
		online()->connect(SNAME("matchmaking_closed"), callable_mp(this, &PurrLobby::matchmaking_closed));
		return;
	}
	if (!p_create) {
		enter(p_code, p_call);
		return;
	}
	Signal created = online()->create_lobby();
	created.connect(callable_mp(this, &PurrLobby::lobby_created).bind(p_call), CONNECT_ONE_SHOT);
}

void PurrLobby::matchmake(const String &p_mode, int p_players, const Ref<RefCounted> &p_call) {
	leave();
	matchmaking_mode = p_mode;
	matchmaking_players = p_players;
	Signal received = online()->get_ice_servers();
	received.connect(callable_mp(this, &PurrLobby::ice_servers_received).bind(p_call, false, String()), CONNECT_ONE_SHOT);
}

void PurrLobby::matched(const String &p_code) {
	const Ref<RefCounted> call = pending_call;
	disconnect_matchmaking();
	matchmade = true;
	enter(p_code, call);
}

void PurrLobby::matchmaking_closed() {
	fail("matchmaking_failed", "Matchmaking stopped before finding a match.");
}

void PurrLobby::disconnect_matchmaking() {
	if (online() && online()->is_connected(SNAME("matchmaking_matched"), callable_mp(this, &PurrLobby::matched))) {
		online()->disconnect(SNAME("matchmaking_matched"), callable_mp(this, &PurrLobby::matched));
		online()->disconnect(SNAME("matchmaking_closed"), callable_mp(this, &PurrLobby::matchmaking_closed));
	}
	if (online() && online()->is_matchmaking()) {
		online()->leave_matchmaking();
	}
	matchmaking_mode = String();
}

void PurrLobby::lobby_created(const Dictionary &p_result, const Ref<RefCounted> &p_call) {
	if (!p_result.get("ok", false)) {
		purr_complete(p_call, failure("lobby_failed", "The online service couldn't create a lobby."));
		return;
	}
	enter(Dictionary(p_result["data"]).get("code", ""), p_call);
}

void PurrLobby::enter(const String &p_code, const Ref<RefCounted> &p_call) {
	if (online()->join_lobby(p_code) != OK) {
		purr_complete(p_call, failure("lobby_failed", "Couldn't connect to the lobby."));
		return;
	}
	code = p_code;
	pending_call = p_call;
	connect_signals();
}

void PurrLobby::connect_signals() {
	online()->connect(SNAME("lobby_joined"), callable_mp(this, &PurrLobby::welcomed));
	online()->connect(SNAME("lobby_peer_joined"), callable_mp(this, &PurrLobby::peer_joined));
	online()->connect(SNAME("lobby_peer_left"), callable_mp(this, &PurrLobby::peer_left));
	online()->connect(SNAME("lobby_signal"), callable_mp(this, &PurrLobby::signal_received));
	online()->connect(SNAME("lobby_closed"), callable_mp(this, &PurrLobby::closed));
	online()->connect(SNAME("lobby_host_changed"), callable_mp(this, &PurrLobby::host_changed));
	online()->connect(SNAME("lobby_kicked"), callable_mp(this, &PurrLobby::kicked));
	online()->connect(SNAME("lobby_result_recorded"), callable_mp(this, &PurrLobby::result_recorded));
	online()->connect(SNAME("lobby_check_result"), callable_mp(this, &PurrLobby::check_result));
	online()->connect(SNAME("lobby_flagged"), callable_mp(this, &PurrLobby::flagged));
}

void PurrLobby::disconnect_signals() {
	if (online() && online()->is_connected(SNAME("lobby_joined"), callable_mp(this, &PurrLobby::welcomed))) {
		online()->disconnect(SNAME("lobby_joined"), callable_mp(this, &PurrLobby::welcomed));
		online()->disconnect(SNAME("lobby_peer_joined"), callable_mp(this, &PurrLobby::peer_joined));
		online()->disconnect(SNAME("lobby_peer_left"), callable_mp(this, &PurrLobby::peer_left));
		online()->disconnect(SNAME("lobby_signal"), callable_mp(this, &PurrLobby::signal_received));
		online()->disconnect(SNAME("lobby_closed"), callable_mp(this, &PurrLobby::closed));
		online()->disconnect(SNAME("lobby_host_changed"), callable_mp(this, &PurrLobby::host_changed));
		online()->disconnect(SNAME("lobby_kicked"), callable_mp(this, &PurrLobby::kicked));
		online()->disconnect(SNAME("lobby_result_recorded"), callable_mp(this, &PurrLobby::result_recorded));
		online()->disconnect(SNAME("lobby_check_result"), callable_mp(this, &PurrLobby::check_result));
		online()->disconnect(SNAME("lobby_flagged"), callable_mp(this, &PurrLobby::flagged));
	}
}

void PurrLobby::fail(const String &p_error, const String &p_message) {
	const Ref<RefCounted> call = pending_call;
	leave();
	if (call.is_valid()) {
		purr_complete(call, failure(p_error, p_message));
	}
}

// The connection to another player, made on first use and added to the multiplayer peer (which
// polls it from then on).
Ref<WebRTCPeerConnection> PurrLobby::connection_to(int64_t p_peer_id) {
	if (peer->has_peer(p_peer_id)) {
		return Dictionary(peer->get_peer(p_peer_id))["connection"];
	}
	Ref<WebRTCPeerConnection> connection = Ref<WebRTCPeerConnection>(WebRTCPeerConnection::create());
	Dictionary config;
	config["iceServers"] = ice_servers;
	if (GLOBAL_GET("lobbies/relay_only")) {
		config["iceTransportPolicy"] = "relay";
	}
	if (connection->initialize(config) != OK) {
		return Ref<WebRTCPeerConnection>();
	}
	connection->connect(SNAME("session_description_created"), callable_mp(this, &PurrLobby::description_created).bind(p_peer_id));
	connection->connect(SNAME("ice_candidate_created"), callable_mp(this, &PurrLobby::candidate_created).bind(p_peer_id));
	peer->add_peer(connection, p_peer_id);
	return connection;
}

// The multiplayer peer for this player's role: the host as server, anyone else as a client who
// offers a connection to the host.
Error PurrLobby::start_peer() {
	if (peer.is_valid()) {
		peer->close();
	}
	peer.instantiate();
	Error error = peer_id == 1 ? peer->create_server() : peer->create_client(peer_id);
	if (error != OK) {
		return error;
	}
	SceneTree::get_singleton()->get_multiplayer()->set_multiplayer_peer(peer);
	if (peer_id != 1) {
		const Ref<WebRTCPeerConnection> host = connection_to(1);
		if (host.is_null() || host->create_offer() != OK) {
			return FAILED;
		}
	}
	return OK;
}

void PurrLobby::welcomed(int64_t p_peer_id, const Array &p_members) {
	peer_id = p_peer_id;
	if (start_peer() != OK) {
		fail("lobby_failed", "Couldn't connect to the host.");
		return;
	}
	Dictionary result;
	result["ok"] = true;
	result["code"] = code;
	purr_complete(pending_call, result);
	pending_call.unref();
	// A ranked match is for the players matchmaking chose: friends don't see it.
	if (!matchmade) {
		Purr::get_singleton()->set_lobby_presence(code, true);
	}
	Lobbies::get_singleton()->call_deferred(SNAME("emit_signal"), SNAME("joined"), code);
}

void PurrLobby::peer_joined(int64_t p_peer_id, const String &p_display_name) {
	// Only the host connects to newcomers; they make the offer.
	if (peer_id == 1 && peer.is_valid()) {
		connection_to(p_peer_id);
	}
}

void PurrLobby::peer_left(int64_t p_peer_id) {
	if (peer.is_valid() && peer->has_peer(p_peer_id)) {
		peer->remove_peer(p_peer_id);
	}
}

void PurrLobby::signal_received(int64_t p_from_peer, const Variant &p_data) {
	// Players only talk to the host, and the host to everyone.
	if (peer.is_null() || p_data.get_type() != Variant::DICTIONARY || (peer_id != 1 && p_from_peer != 1)) {
		return;
	}
	const Dictionary data = p_data;
	if (data.has("sdp")) {
		const Ref<WebRTCPeerConnection> connection = connection_to(p_from_peer);
		if (connection.is_valid()) {
			connection->set_remote_description(data.get("type", ""), data["sdp"]);
		}
	} else if (data.has("candidate") && peer->has_peer(p_from_peer)) {
		const Dictionary candidate = data["candidate"];
		const Ref<WebRTCPeerConnection> connection = connection_to(p_from_peer);
		connection->add_ice_candidate(candidate.get("media", ""), candidate.get("index", 0), candidate.get("name", ""));
	}
}

void PurrLobby::closed() {
	if (pending_call.is_valid()) {
		fail("lobby_failed", "Couldn't join the lobby: it doesn't exist, it's full, or the online service can't be reached.");
		return;
	}
	// The game carries on peer to peer; only players joining later needed the lobby, so friends
	// no longer see it.
	disconnect_signals();
	Purr::get_singleton()->set_lobby_presence(String(), true);
}

// The host left. With host migration on, whoever's been here longest takes over as peer 1, and
// the rest connect to them; the game keeps going (Purr emits host_migrated). Without it, the
// connection to the old host has already ended and the game decides what to do.
void PurrLobby::host_changed(int64_t p_old_peer_id, bool p_you) {
	if (peer.is_null() || !GLOBAL_GET("lobbies/host_migration")) {
		return;
	}
	if (p_you) {
		peer_id = 1;
	}
	if (start_peer() != OK) {
		fail("lobby_failed", "Couldn't connect to the new host.");
		return;
	}
	Lobbies::get_singleton()->call_deferred(SNAME("emit_signal"), SNAME("host_changed"), p_you);
}

void PurrLobby::result_recorded() {
	Lobbies::get_singleton()->call_deferred(SNAME("emit_signal"), SNAME("result_recorded"));
}

void PurrLobby::check_result(const String &p_name, bool p_agreed, const Array &p_disagreeing) {
	Lobbies::get_singleton()->call_deferred(SNAME("emit_signal"), SNAME("check_completed"), p_name, p_agreed, p_disagreeing);
}

void PurrLobby::flagged(int64_t p_peer_id, const String &p_reason) {
	Lobbies::get_singleton()->call_deferred(SNAME("emit_signal"), SNAME("player_flagged"), p_peer_id, p_reason);
}

void PurrLobby::kicked() {
	leave();
	Lobbies::get_singleton()->call_deferred(SNAME("emit_signal"), SNAME("kicked"));
}

void PurrLobby::description_created(const String &p_type, const String &p_sdp, int64_t p_to_peer) {
	if (peer.is_null() || !peer->has_peer(p_to_peer)) {
		return;
	}
	connection_to(p_to_peer)->set_local_description(p_type, p_sdp);
	if (!online()->is_in_lobby()) {
		return;
	}
	Dictionary data;
	data["type"] = p_type;
	data["sdp"] = p_sdp;
	online()->send_signal(p_to_peer, data);
}

void PurrLobby::candidate_created(const String &p_media, int64_t p_index, const String &p_name, int64_t p_to_peer) {
	if (!online()->is_in_lobby()) {
		return;
	}
	Dictionary candidate;
	candidate["media"] = p_media;
	candidate["index"] = p_index;
	candidate["name"] = p_name;
	Dictionary data;
	data["candidate"] = candidate;
	online()->send_signal(p_to_peer, data);
}

void PurrLobby::leave() {
	if (!code.is_empty() && Purr::get_singleton()) {
		Purr::get_singleton()->set_lobby_presence(String(), true);
	}
	disconnect_matchmaking();
	disconnect_signals();
	if (online() && online()->is_in_lobby()) {
		online()->leave_lobby();
	}
	if (peer.is_valid()) {
		SceneTree *tree = SceneTree::get_singleton();
		if (tree && tree->get_multiplayer()->get_multiplayer_peer() == peer) {
			tree->get_multiplayer()->set_multiplayer_peer(Ref<MultiplayerPeer>());
		}
		peer->close();
		peer.unref();
	}
	code = String();
	peer_id = 0;
	matchmade = false;
	pending_call.unref();
}

PurrLobby::~PurrLobby() {
	disconnect_matchmaking();
	disconnect_signals();
}
