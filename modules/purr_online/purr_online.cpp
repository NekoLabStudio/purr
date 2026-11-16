#include "purr_online.h"

#include "purr_http.h"

#include "core/config/project_settings.h"
#include "core/crypto/crypto.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/main/scene_tree.h"

PurrOnline *PurrOnline::singleton = nullptr;

void PurrOnlineCall::_bind_methods() {
	ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::DICTIONARY, "result")));
}

PurrOnline::PurrOnline() {
	singleton = this;
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::STRING, "online/url", PROPERTY_HINT_PLACEHOLDER_TEXT, "https://purr-online.example.workers.dev"), "");
	GLOBAL_DEF_BASIC("online/game", "");
}

PurrOnline::~PurrOnline() {
	leave_lobby();
	leave_matchmaking();
	singleton = nullptr;
}

bool PurrOnline::is_configured() const {
	return !get_url().is_empty();
}

// Tokens, device secrets and codes only ever travel encrypted: plain http is for a copy of the
// service running on this computer.
bool PurrOnline::is_secure() const {
	const String url = get_url();
	if (url.begins_with("https://")) {
		return true;
	}
	const String host = url.trim_prefix("http://").get_slice("/", 0).get_slice(":", 0);
	return url.begins_with("http://") && (host == "localhost" || host == "127.0.0.1");
}

String PurrOnline::get_url() const {
	return String(GLOBAL_GET("online/url")).strip_edges().trim_suffix("/");
}

String PurrOnline::get_game() const {
	return GLOBAL_GET("online/game");
}

void PurrOnline::set_session(const String &p_token, const String &p_account_id) {
	token = p_token;
	account_id = p_account_id;
}

void PurrOnline::sign_out() {
	leave_lobby();
	leave_matchmaking();
	token = String();
	account_id = String();
}

Signal PurrOnline::call(HTTPClient::Method p_method, const String &p_path, const Variant &p_body, const String &p_token, const Vector<String> &p_extra_headers, bool p_binary_answer) {
	Ref<PurrOnlineCall> pending_call;
	pending_call.instantiate();
	pending.insert(pending_call);
	const Signal completed(pending_call.ptr(), SNAME("completed"));

	Vector<String> headers = p_extra_headers;
	headers.append_array(game_headers());
	const String bearer = p_token.is_empty() ? token : p_token;
	if (!bearer.is_empty()) {
		headers.push_back("Authorization: Bearer " + bearer);
	}
	PackedByteArray body;
	if (p_body.get_type() == Variant::PACKED_BYTE_ARRAY) {
		body = p_body;
		headers.push_back("Content-Type: application/octet-stream");
	} else if (p_body.get_type() != Variant::NIL) {
		body = JSON::stringify(p_body).to_utf8_buffer();
		headers.push_back("Content-Type: application/json");
	}

	// Signing in, and adding an email (which can move this device to the address's account).
	const bool signs_in = p_path.begins_with("/auth/");
	if (!is_configured() || !is_secure()) {
		Dictionary response;
		response["status"] = 0;
		response["error"] = is_configured() ? "The online service must be reached over https (plain http only for this computer)." : "The online service isn't set up (online/url).";
		callable_mp(this, &PurrOnline::finish).call_deferred(response, pending_call, p_binary_answer, false);
		return completed;
	}
	// After the service turned this version away (or turned out too old), nothing else is asked
	// of it this session, except what it is.
	if (!blocked.is_empty() && p_path != "/version") {
		Dictionary response;
		response["status"] = 0;
		response["error_code"] = blocked;
		response["error"] = blocked == "service_outdated" ? "The online service is too old for this version of Purr." : "This version of the game can't play online: update it.";
		callable_mp(this, &PurrOnline::finish).call_deferred(response, pending_call, p_binary_answer, false);
		return completed;
	}
	purr_http_request(p_method, get_url() + p_path, headers, body, callable_mp(this, &PurrOnline::finish).bind(pending_call, p_binary_answer, signs_in));
	return completed;
}

void PurrOnline::finish(const Dictionary &p_response, const Ref<PurrOnlineCall> &p_call, bool p_binary_answer, bool p_signs_in) {
	const int status = p_response.get("status", 0);
	const PackedByteArray body = p_response.get("body", PackedByteArray());

	Dictionary result;
	result["status"] = status;
	result["ok"] = status >= 200 && status < 300;
	result["headers"] = p_response.get("headers", Dictionary());
	result["error"] = String();
	result["message"] = String();

	if (status == 0) {
		result["error"] = p_response.get("error_code", "unreachable");
		result["message"] = p_response.get("error", "");
	} else if (p_binary_answer && (bool)result["ok"]) {
		result["data"] = body;
	} else {
		const Variant parsed = JSON::parse_string(String::utf8((const char *)body.ptr(), body.size()));
		result["data"] = parsed;
		if (!(bool)result["ok"] && parsed.get_type() == Variant::DICTIONARY) {
			const Dictionary error = parsed;
			result["error"] = error.get("error", "");
			result["message"] = error.get("message", "");
		}
	}

	check_versions(result);

	// Signing in (or adding an email, which can move this device to another account) keeps the
	// session.
	if (p_signs_in && (bool)result["ok"] && result["data"].get_type() == Variant::DICTIONARY) {
		const Dictionary data = result["data"];
		if (data.has("token") && data.has("account_id")) {
			set_session(data["token"], data["account_id"]);
		}
	}

	p_call->emit_signal(SNAME("completed"), result);
	pending.erase(p_call);
}

// What every request says about the game: its ID and version, which the service can require a
// minimum or maximum of.
Vector<String> PurrOnline::game_headers() const {
	Vector<String> headers;
	headers.push_back("X-Purr-Game: " + get_game());
	const String version = String(GLOBAL_GET("application/config/version")).strip_edges();
	if (!version.is_empty()) {
		headers.push_back("X-Game-Version: " + version);
	}
	return headers;
}

// The service's answer about versions: this game's version turned away (update_required), or the
// service older than this Purr needs (its protocol, on every answer). Either stops online play
// for the session, once, with a signal the game can show.
void PurrOnline::check_versions(const Dictionary &p_result) {
	if (!blocked.is_empty()) {
		return;
	}
	const String error = p_result.get("error", "");
	if (error == "update_required" || error == "version_unsupported") {
		blocked = error;
		const Dictionary data = p_result.get("data", Dictionary());
		emit_signal(SNAME("update_required"), data.get("min_version", Variant()), data.get("max_version", Variant()));
		return;
	}
	const Dictionary headers = p_result.get("headers", Dictionary());
	if (headers.has("x-purr-protocol") && String(headers["x-purr-protocol"]).to_int() < PROTOCOL) {
		blocked = "service_outdated";
		emit_signal(SNAME("service_outdated"));
	}
}

Signal PurrOnline::get_service_version() {
	return call(HTTPClient::METHOD_GET, "/version", Variant(), String());
}

static Dictionary with_name(Dictionary p_body, const String &p_display_name) {
	if (!p_display_name.is_empty()) {
		p_body["display_name"] = p_display_name;
	}
	return p_body;
}

Signal PurrOnline::sign_in_device(const String &p_secret, const String &p_display_name) {
	Dictionary body;
	body["game"] = get_game();
	body["secret"] = p_secret;
	return call(HTTPClient::METHOD_POST, "/auth/device", with_name(body, p_display_name), String());
}

Signal PurrOnline::sign_in_steam(const String &p_ticket_hex, const String &p_display_name) {
	Dictionary body;
	body["game"] = get_game();
	body["ticket"] = p_ticket_hex;
	return call(HTTPClient::METHOD_POST, "/auth/steam", with_name(body, p_display_name), String());
}

Signal PurrOnline::sign_in_game_center(const Dictionary &p_proof, const String &p_display_name) {
	Dictionary body = p_proof.duplicate();
	body["game"] = get_game();
	return call(HTTPClient::METHOD_POST, "/auth/game_center", with_name(body, p_display_name), String());
}

Signal PurrOnline::sign_in_play_games(const String &p_server_auth_code, const String &p_display_name) {
	Dictionary body;
	body["game"] = get_game();
	body["server_auth_code"] = p_server_auth_code;
	return call(HTTPClient::METHOD_POST, "/auth/play_games", with_name(body, p_display_name), String());
}

Signal PurrOnline::get_account(const String &p_token) {
	return call(HTTPClient::METHOD_GET, "/account", Variant(), p_token);
}

Signal PurrOnline::set_display_name(const String &p_name) {
	Dictionary body;
	body["display_name"] = p_name;
	return call(HTTPClient::METHOD_PUT, "/account/name", body, String());
}

Signal PurrOnline::delete_account() {
	return call(HTTPClient::METHOD_DELETE, "/account", Variant(), String());
}

Signal PurrOnline::list_saves(const String &p_token) {
	return call(HTTPClient::METHOD_GET, "/saves/" + get_game().uri_encode(), Variant(), p_token);
}

Signal PurrOnline::read_save(const String &p_slot, const String &p_token) {
	return call(HTTPClient::METHOD_GET, "/saves/" + get_game().uri_encode() + "/" + p_slot.uri_encode(), Variant(), p_token, Vector<String>(), true);
}

Signal PurrOnline::write_save(const String &p_slot, const PackedByteArray &p_data, int64_t p_replaces_version, const Dictionary &p_summary, const String &p_token) {
	Vector<String> headers;
	headers.push_back("X-Save-Version: " + itos(p_replaces_version));
	headers.push_back("X-Save-Summary: " + JSON::stringify(p_summary));
	return call(HTTPClient::METHOD_PUT, "/saves/" + get_game().uri_encode() + "/" + p_slot.uri_encode(), p_data, p_token, headers);
}

Signal PurrOnline::email_start(const String &p_email) {
	Dictionary body;
	body["email"] = p_email;
	body["game"] = get_game();
	return call(HTTPClient::METHOD_POST, "/auth/email/start", body, String());
}

Signal PurrOnline::email_verify(const String &p_email, const String &p_challenge, const String &p_code, const String &p_current_email, const String &p_display_name) {
	Dictionary body;
	body["email"] = p_email;
	body["challenge"] = p_challenge;
	body["code"] = p_code;
	if (!p_current_email.is_empty()) {
		body["current_email"] = p_current_email;
	}
	return call(HTTPClient::METHOD_POST, "/auth/email/verify", with_name(body, p_display_name), String());
}

Signal PurrOnline::guest_email_start(const String &p_email) {
	Dictionary body;
	body["email"] = p_email;
	body["game"] = get_game();
	return call(HTTPClient::METHOD_POST, "/guest/email/start", body, String());
}

Signal PurrOnline::guest_email_verify(const String &p_email, const String &p_challenge, const String &p_code, bool p_remember, const String &p_device_label, const String &p_display_name) {
	Dictionary body;
	body["email"] = p_email;
	body["challenge"] = p_challenge;
	body["code"] = p_code;
	body["game"] = get_game();
	body["remember"] = p_remember;
	body["device_label"] = p_device_label;
	return call(HTTPClient::METHOD_POST, "/guest/email/verify", with_name(body, p_display_name), String());
}

Signal PurrOnline::list_grants() {
	return call(HTTPClient::METHOD_GET, "/grants", Variant(), String());
}

Signal PurrOnline::revoke_grant(const String &p_grant_id) {
	return call(HTTPClient::METHOD_DELETE, "/grants/" + p_grant_id.uri_encode(), Variant(), String());
}

Signal PurrOnline::forget_grant(const String &p_grant_token) {
	return call(HTTPClient::METHOD_DELETE, "/grants/current", Variant(), p_grant_token);
}

Signal PurrOnline::create_lobby() {
	Dictionary body;
	body["game"] = get_game();
	return call(HTTPClient::METHOD_POST, "/lobbies", body, String());
}


// A WebSocket to the service, signed in, polled every frame by p_poll.
Error PurrOnline::open_socket(Ref<WebSocketPeer> &r_socket, const String &p_path, const Callable &p_poll) {
	ERR_FAIL_COND_V_MSG(!is_configured(), ERR_UNCONFIGURED, "The online service isn't set up (online/url).");
	ERR_FAIL_COND_V_MSG(token.is_empty(), ERR_UNAUTHORIZED, "Sign in first.");
	ERR_FAIL_COND_V_MSG(!is_secure(), ERR_UNAUTHORIZED, "The online service must be reached over https (plain http only for this computer).");
	ERR_FAIL_COND_V_MSG(!blocked.is_empty(), ERR_UNAVAILABLE, "Online play is off for this session: " + blocked + ".");
	r_socket = Ref<WebSocketPeer>(WebSocketPeer::create());
	Vector<String> headers = game_headers();
	headers.push_back("Authorization: Bearer " + token);
	r_socket->set_handshake_headers(headers);
	const String url = get_url().replace_first("https://", "wss://").replace_first("http://", "ws://") + p_path;
	const Error error = r_socket->connect_to_url(url, url.begins_with("wss://") ? TLSOptions::client() : Ref<TLSOptions>());
	if (error != OK) {
		r_socket.unref();
		return error;
	}
	SceneTree::get_singleton()->connect(SNAME("process_frame"), p_poll);
	return OK;
}

void PurrOnline::close_socket(Ref<WebSocketPeer> &r_socket, const Callable &p_poll) {
	if (r_socket.is_null()) {
		return;
	}
	r_socket->close();
	r_socket.unref();
	SceneTree *tree = SceneTree::get_singleton();
	if (tree && tree->is_connected(SNAME("process_frame"), p_poll)) {
		tree->disconnect(SNAME("process_frame"), p_poll);
	}
}

// Every JSON message waiting on the socket.
static Vector<Dictionary> read_messages(const Ref<WebSocketPeer> &p_socket) {
	p_socket->poll();
	Vector<Dictionary> messages;
	while (p_socket->get_available_packet_count() > 0) {
		Vector<uint8_t> packet;
		if (p_socket->get_packet_buffer(packet) != OK || !p_socket->was_string_packet()) {
			continue;
		}
		const Variant parsed = JSON::parse_string(String::utf8((const char *)packet.ptr(), packet.size()));
		if (parsed.get_type() == Variant::DICTIONARY) {
			messages.push_back(parsed);
		}
	}
	return messages;
}

Error PurrOnline::join_lobby(const String &p_code) {
	leave_lobby();
	kicked_seen = false;
	return open_socket(lobby, "/lobbies/" + p_code.uri_encode() + "/socket?game=" + get_game().uri_encode(), callable_mp(this, &PurrOnline::poll_lobby));
}

void PurrOnline::send_lobby_message(const Dictionary &p_message) {
	ERR_FAIL_COND_MSG(lobby.is_null() || lobby->get_ready_state() != WebSocketPeer::STATE_OPEN, "Not in a lobby.");
	lobby->send_text(JSON::stringify(p_message));
}

void PurrOnline::send_signal(int64_t p_to_peer, const Variant &p_data) {
	Dictionary message;
	message["type"] = "signal";
	message["to"] = p_to_peer;
	message["data"] = p_data;
	send_lobby_message(message);
}

void PurrOnline::kick_peer(int64_t p_peer_id) {
	Dictionary message;
	message["type"] = "kick";
	message["peer_id"] = p_peer_id;
	send_lobby_message(message);
}

void PurrOnline::report_peer(int64_t p_peer_id, const String &p_reason, const String &p_details) {
	Dictionary message;
	message["type"] = "report";
	message["peer_id"] = p_peer_id;
	message["reason"] = p_reason;
	message["details"] = p_details;
	send_lobby_message(message);
}

void PurrOnline::block_peer(int64_t p_peer_id) {
	Dictionary message;
	message["type"] = "block";
	message["peer_id"] = p_peer_id;
	send_lobby_message(message);
}

void PurrOnline::report_result(const Array &p_ranking) {
	Dictionary message;
	message["type"] = "result";
	message["ranking"] = p_ranking;
	send_lobby_message(message);
}

void PurrOnline::send_check(const String &p_name, const String &p_value) {
	Dictionary message;
	message["type"] = "check";
	message["name"] = p_name;
	message["value"] = p_value;
	send_lobby_message(message);
}

void PurrOnline::flag_peer(int64_t p_peer_id, const String &p_reason) {
	Dictionary message;
	message["type"] = "flag";
	message["peer_id"] = p_peer_id;
	message["reason"] = p_reason;
	send_lobby_message(message);
}

void PurrOnline::leave_lobby() {
	close_socket(lobby, callable_mp(this, &PurrOnline::poll_lobby));
}

void PurrOnline::poll_lobby() {
	// The game may leave (or join another lobby) from any of the signals below, so the messages
	// are read first and the socket is held here while they're handed out.
	const Ref<WebSocketPeer> socket = lobby;
	if (socket.is_null()) {
		return;
	}
	const Vector<Dictionary> messages = read_messages(socket);
	const bool closed = socket->get_ready_state() == WebSocketPeer::STATE_CLOSED;

	for (const Dictionary &message : messages) {
		if (lobby != socket) {
			return;
		}
		const String type = message.get("type", "");
		if (type == "welcome") {
			emit_signal(SNAME("lobby_joined"), message.get("peer_id", 0), message.get("members", Array()), message.get("ice_servers", Array()));
		} else if (type == "joined") {
			emit_signal(SNAME("lobby_peer_joined"), message.get("peer_id", 0), message.get("display_name", ""));
		} else if (type == "left") {
			emit_signal(SNAME("lobby_peer_left"), message.get("peer_id", 0));
		} else if (type == "signal") {
			emit_signal(SNAME("lobby_signal"), message.get("from", 0), message.get("data", Variant()));
		} else if (type == "host_changed") {
			emit_signal(SNAME("lobby_host_changed"), message.get("old_peer_id", 0), message.get("you", false));
		} else if (type == "kicked") {
			kicked_seen = true;
			emit_signal(SNAME("lobby_kicked"));
		} else if (type == "result_recorded") {
			emit_signal(SNAME("lobby_result_recorded"));
		} else if (type == "flagged") {
			emit_signal(SNAME("lobby_flagged"), message.get("peer_id", 0), message.get("reason", ""));
		} else if (type == "check_result") {
			emit_signal(SNAME("lobby_check_result"), message.get("name", ""), message.get("agreed", false), message.get("disagreeing", Array()));
		}
	}
	if (closed && lobby == socket) {
		// A kick closes with its own code, so it's known even if the "kicked" message was lost
		// as the socket closed.
		const bool kicked = socket->get_close_code() == LOBBY_KICKED_CLOSE_CODE && !kicked_seen;
		kicked_seen = false;
		leave_lobby();
		if (kicked) {
			emit_signal(SNAME("lobby_kicked"));
		}
		emit_signal(SNAME("lobby_closed"));
	}
}

// Matchmaking: waits on the service until it finds players of a similar rating for this game's
// ranked mode, then hands over the code of a new lobby to join.
Error PurrOnline::join_matchmaking(const String &p_mode, int p_players) {
	leave_matchmaking();
	return open_socket(matchmaking, "/matchmaking/" + get_game().uri_encode() + "/" + p_mode.uri_encode() + "/socket?players=" + itos(p_players), callable_mp(this, &PurrOnline::poll_matchmaking));
}

void PurrOnline::leave_matchmaking() {
	close_socket(matchmaking, callable_mp(this, &PurrOnline::poll_matchmaking));
}

void PurrOnline::poll_matchmaking() {
	const Ref<WebSocketPeer> socket = matchmaking;
	if (socket.is_null()) {
		return;
	}
	const Vector<Dictionary> messages = read_messages(socket);
	const bool closed = socket->get_ready_state() == WebSocketPeer::STATE_CLOSED;
	for (const Dictionary &message : messages) {
		if (matchmaking != socket) {
			return;
		}
		const String type = message.get("type", "");
		if (type == "waiting") {
			emit_signal(SNAME("matchmaking_waiting"), message.get("players", 0));
		} else if (type == "matched") {
			leave_matchmaking();
			emit_signal(SNAME("matchmaking_matched"), message.get("code", ""));
			return;
		}
	}
	if (closed && matchmaking == socket) {
		leave_matchmaking();
		emit_signal(SNAME("matchmaking_closed"));
	}
}

Signal PurrOnline::get_rating(const String &p_mode) {
	return call(HTTPClient::METHOD_GET, "/ratings/" + get_game().uri_encode() + "/" + p_mode.uri_encode(), Variant(), String());
}

void PurrOnline::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_configured"), &PurrOnline::is_configured);
	ClassDB::bind_method(D_METHOD("get_blocked_reason"), &PurrOnline::get_blocked_reason);
	ClassDB::bind_method(D_METHOD("get_service_version"), &PurrOnline::get_service_version);
	ClassDB::bind_method(D_METHOD("get_url"), &PurrOnline::get_url);
	ClassDB::bind_method(D_METHOD("get_game"), &PurrOnline::get_game);
	ClassDB::bind_method(D_METHOD("get_token"), &PurrOnline::get_token);
	ClassDB::bind_method(D_METHOD("get_account_id"), &PurrOnline::get_account_id);
	ClassDB::bind_method(D_METHOD("set_session", "token", "account_id"), &PurrOnline::set_session);
	ClassDB::bind_method(D_METHOD("sign_out"), &PurrOnline::sign_out);

	ClassDB::bind_method(D_METHOD("sign_in_device", "secret", "display_name"), &PurrOnline::sign_in_device, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("sign_in_steam", "ticket_hex", "display_name"), &PurrOnline::sign_in_steam, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("sign_in_game_center", "proof", "display_name"), &PurrOnline::sign_in_game_center, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("sign_in_play_games", "server_auth_code", "display_name"), &PurrOnline::sign_in_play_games, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("get_account", "token"), &PurrOnline::get_account, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("set_display_name", "name"), &PurrOnline::set_display_name);
	ClassDB::bind_method(D_METHOD("delete_account"), &PurrOnline::delete_account);

	ClassDB::bind_method(D_METHOD("list_saves", "token"), &PurrOnline::list_saves, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("read_save", "slot", "token"), &PurrOnline::read_save, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("write_save", "slot", "data", "replaces_version", "summary", "token"), &PurrOnline::write_save, DEFVAL(Dictionary()), DEFVAL(String()));

	ClassDB::bind_method(D_METHOD("email_start", "email"), &PurrOnline::email_start);
	ClassDB::bind_method(D_METHOD("email_verify", "email", "challenge", "code", "current_email", "display_name"), &PurrOnline::email_verify, DEFVAL(String()), DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("guest_email_start", "email"), &PurrOnline::guest_email_start);
	ClassDB::bind_method(D_METHOD("guest_email_verify", "email", "challenge", "code", "remember", "device_label", "display_name"), &PurrOnline::guest_email_verify, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("list_grants"), &PurrOnline::list_grants);
	ClassDB::bind_method(D_METHOD("revoke_grant", "grant_id"), &PurrOnline::revoke_grant);
	ClassDB::bind_method(D_METHOD("forget_grant", "grant_token"), &PurrOnline::forget_grant);

	ClassDB::bind_method(D_METHOD("create_lobby"), &PurrOnline::create_lobby);
	ClassDB::bind_method(D_METHOD("join_lobby", "code"), &PurrOnline::join_lobby);
	ClassDB::bind_method(D_METHOD("send_signal", "to_peer", "data"), &PurrOnline::send_signal);
	ClassDB::bind_method(D_METHOD("leave_lobby"), &PurrOnline::leave_lobby);
	ClassDB::bind_method(D_METHOD("kick_peer", "peer_id"), &PurrOnline::kick_peer);
	ClassDB::bind_method(D_METHOD("report_peer", "peer_id", "reason", "details"), &PurrOnline::report_peer, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("block_peer", "peer_id"), &PurrOnline::block_peer);
	ClassDB::bind_method(D_METHOD("report_result", "ranking"), &PurrOnline::report_result);
	ClassDB::bind_method(D_METHOD("send_check", "name", "value"), &PurrOnline::send_check);
	ClassDB::bind_method(D_METHOD("flag_peer", "peer_id", "reason"), &PurrOnline::flag_peer);
	ClassDB::bind_method(D_METHOD("join_matchmaking", "mode", "players"), &PurrOnline::join_matchmaking, DEFVAL(2));
	ClassDB::bind_method(D_METHOD("leave_matchmaking"), &PurrOnline::leave_matchmaking);
	ClassDB::bind_method(D_METHOD("is_matchmaking"), &PurrOnline::is_matchmaking);
	ClassDB::bind_method(D_METHOD("get_rating", "mode"), &PurrOnline::get_rating);
	ClassDB::bind_method(D_METHOD("is_in_lobby"), &PurrOnline::is_in_lobby);

	ADD_SIGNAL(MethodInfo("lobby_joined", PropertyInfo(Variant::INT, "peer_id"), PropertyInfo(Variant::ARRAY, "members"), PropertyInfo(Variant::ARRAY, "ice_servers")));
	ADD_SIGNAL(MethodInfo("lobby_peer_joined", PropertyInfo(Variant::INT, "peer_id"), PropertyInfo(Variant::STRING, "display_name")));
	ADD_SIGNAL(MethodInfo("lobby_peer_left", PropertyInfo(Variant::INT, "peer_id")));
	ADD_SIGNAL(MethodInfo("lobby_signal", PropertyInfo(Variant::INT, "from_peer"), PropertyInfo(Variant::NIL, "data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
	ADD_SIGNAL(MethodInfo("lobby_closed"));
	ADD_SIGNAL(MethodInfo("update_required", PropertyInfo(Variant::NIL, "min_version", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT), PropertyInfo(Variant::NIL, "max_version", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
	ADD_SIGNAL(MethodInfo("service_outdated"));
	ADD_SIGNAL(MethodInfo("lobby_host_changed", PropertyInfo(Variant::INT, "old_peer_id"), PropertyInfo(Variant::BOOL, "you")));
	ADD_SIGNAL(MethodInfo("lobby_kicked"));
	ADD_SIGNAL(MethodInfo("lobby_result_recorded"));
	ADD_SIGNAL(MethodInfo("lobby_flagged", PropertyInfo(Variant::INT, "peer_id"), PropertyInfo(Variant::STRING, "reason")));
	ADD_SIGNAL(MethodInfo("lobby_check_result", PropertyInfo(Variant::STRING, "name"), PropertyInfo(Variant::BOOL, "agreed"), PropertyInfo(Variant::ARRAY, "disagreeing")));
	ADD_SIGNAL(MethodInfo("matchmaking_waiting", PropertyInfo(Variant::INT, "players")));
	ADD_SIGNAL(MethodInfo("matchmaking_matched", PropertyInfo(Variant::STRING, "code")));
	ADD_SIGNAL(MethodInfo("matchmaking_closed"));
}
