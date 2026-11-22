#include "purr_game_center_lobby.h"

#include "purr.h"

#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/math/math_funcs.h"
#include "core/math/random_number_generator.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "scene/main/multiplayer_api.h"
#include "scene/main/scene_tree.h"

namespace {

const char *PEER_CLASS = "GameCenterMultiplayerPeer";
// GameKit's party code alphabet (GKGameActivity.validPartyCodeAlphabet).
const char *PARTY_ALPHABET = "23456789CFGHJMPQRVWX";
// How long joining with a code waits for the lobby's host, and each widening of matchmaking.
const double JOIN_TIMEOUT = 30.0;
const double WIDEN_AFTER = 5.0;
const int RATING_BANDS[] = { 100, 200, 400 };
const int LAST_SEARCH_LEVEL = 3;

// A new party code: two halves of four characters.
String new_party_code() {
	const String alphabet = PARTY_ALPHABET;
	Ref<RandomNumberGenerator> random;
	random.instantiate();
	random->randomize();
	String code;
	for (int i = 0; i < 8; i++) {
		if (i == 4) {
			code += "-";
		}
		code += String::chr(alphabet[random->randi_range(0, alphabet.length() - 1)]);
	}
	return code;
}

// The player group for one widening of the search: everyone at the same level, in the same mode,
// size and rating band, waits in the same group. Never 0, which isn't a group.
int64_t player_group(const String &p_mode, int p_players, int p_level, double p_rating) {
	String key = vformat("%s|%d|%d", p_mode, p_players, p_level);
	if (p_level < LAST_SEARCH_LEVEL) {
		key += "|" + itos((int64_t)Math::floor(p_rating / RATING_BANDS[p_level]));
	}
	return (int64_t)(key.hash() & 0x7FFFFFFF) | 1;
}

} // namespace

bool PurrGameCenterLobby::is_supported() {
	return ClassDB::class_exists(PEER_CLASS);
}

bool PurrGameCenterLobby::has_parties() {
	return !String(GLOBAL_GET("game_center/party_activity")).is_empty();
}

String PurrGameCenterLobby::_own_member() const {
	return peer.is_valid() ? String(peer->call("get_local_player_id")) : String();
}

String PurrGameCenterLobby::host() const {
	return peer.is_valid() ? String(peer->call("get_host")) : String();
}

void PurrGameCenterLobby::new_peer() {
	peer = Ref<MultiplayerPeer>(Object::cast_to<MultiplayerPeer>(ClassDB::instantiate(PEER_CLASS)));
	ERR_FAIL_COND(peer.is_null());
	peer->connect(SNAME("match_started"), callable_mp(this, &PurrGameCenterLobby::match_started));
	peer->connect(SNAME("match_failed"), callable_mp(this, &PurrGameCenterLobby::match_failed));
	peer->connect(SNAME("player_connected"), callable_mp(this, &PurrGameCenterLobby::player_connected));
	peer->connect(SNAME("player_disconnected"), callable_mp(this, &PurrGameCenterLobby::player_disconnected));
	peer->connect(SNAME("message_received"), callable_mp(this, &PurrGameCenterLobby::message_received));
}

// Godot's multiplayer starts afresh with the peer (also after a new host, when every peer ID it
// knew may have changed).
void PurrGameCenterLobby::set_tree_peer() {
	SceneTree *tree = SceneTree::get_singleton();
	if (tree && peer.is_valid()) {
		tree->get_multiplayer()->set_multiplayer_peer(Ref<MultiplayerPeer>());
		tree->get_multiplayer()->set_multiplayer_peer(peer);
	}
}

void PurrGameCenterLobby::after(double p_seconds, uint64_t p_generation) {
	SceneTree *tree = SceneTree::get_singleton();
	ERR_FAIL_NULL(tree);
	tree->create_timer(p_seconds)->connect(SNAME("timeout"), callable_mp(this, is_matchmaking() ? &PurrGameCenterLobby::widen : &PurrGameCenterLobby::timed_out).bind(p_generation), CONNECT_ONE_SHOT);
}

void PurrGameCenterLobby::create(const Ref<RefCounted> &p_call) {
	leave();
	new_peer();
	pending_call = p_call;
	const String own = _own_member();
	peer->call("set_host", own);
	member_joined(own, 1);
	host_is(own, false);
	// With parties, a code others join with; without (or before OS 26), friends join by invite.
	String party_code = has_parties() ? new_party_code() : String();
	if (!party_code.is_empty() && (Error)(int)peer->call("find_party_match", GLOBAL_GET("game_center/party_activity"), party_code) != OK) {
		party_code = String();
	}
	set_tree_peer();
	entered(party_code);
}

void PurrGameCenterLobby::join(const String &p_code, const Ref<RefCounted> &p_call) {
	leave();
	if (!has_parties()) {
		if (p_call.is_valid()) {
			purr_complete(p_call, purr_failure("unavailable", "Joining with a code on Game Center needs a party activity (game_center/party_activity); invite friends instead."));
		}
		return;
	}
	new_peer();
	pending_call = p_call;
	joining = true;
	joining_code = p_code.to_upper();
	if ((Error)(int)peer->call("find_party_match", GLOBAL_GET("game_center/party_activity"), joining_code) != OK) {
		failed("unavailable", "Joining with a code on Game Center needs iOS, macOS, tvOS or visionOS 26 or later.");
		return;
	}
	after(JOIN_TIMEOUT, ++generation);
}

void PurrGameCenterLobby::join_invite(const String &p_sender_id) {
	leave();
	new_peer();
	joining = true;
	joining_code = String();
	peer->call("set_host", p_sender_id);
	if ((Error)(int)peer->call("accept_invite") != OK) {
		leave();
		return;
	}
	after(JOIN_TIMEOUT, ++generation);
}

void PurrGameCenterLobby::timed_out(uint64_t p_generation) {
	if (p_generation == generation && joining) {
		failed("not_found", "Nobody's hosting a lobby with that code.");
	}
}

void PurrGameCenterLobby::invite_with_platform_screen() {
	ERR_FAIL_COND_MSG(!active, "Create or join a lobby before inviting friends.");
	peer->call("invite", (int64_t)GLOBAL_GET("lobbies/max_players"));
}

void PurrGameCenterLobby::invite(const String &p_friend_id) {
	ERR_FAIL_COND_MSG(!active, "Create or join a lobby before inviting friends.");
	peer->call("invite_player", p_friend_id, (int64_t)GLOBAL_GET("lobbies/max_players"));
}

void PurrGameCenterLobby::match_started() {
	if (is_matchmaking()) {
		begin_match();
		return;
	}
	for (const Variant &player : Array(peer->call("get_player_ids"))) {
		player_connected(player);
	}
}

void PurrGameCenterLobby::match_failed(const String &p_error, const String &p_message) {
	if (is_matchmaking()) {
		if (p_error != "cancelled") {
			failed(p_error, p_message);
		}
		return;
	}
	if (joining) {
		failed(p_error == "cancelled" ? "cancelled" : "lobby_failed", p_message);
		return;
	}
	// The host keeps its lobby: friends can still be invited.
	if (p_error != "cancelled") {
		WARN_PRINT(vformat("Game Center couldn't start the lobby's party: %s", p_message));
	}
}

void PurrGameCenterLobby::player_connected(const String &p_player_id) {
	if (is_matchmaking()) {
		return;
	}
	member_joined(p_player_id, peer->call("get_peer_id_for_player", p_player_id));
	if (is_host()) {
		Dictionary message;
		message["t"] = "host";
		_send(message);
	}
	ready_if_host_here();
}

// A joining player is in once its host is there.
void PurrGameCenterLobby::ready_if_host_here() {
	const String current_host = host();
	if (!joining || current_host.is_empty() || !Array(peer->call("get_player_ids")).has(current_host)) {
		return;
	}
	joining = false;
	generation++;
	member_joined(_own_member(), peer->call("get_peer_id_for_player", _own_member()));
	host_is(current_host, false);
	set_tree_peer();
	entered(joining_code);
}

void PurrGameCenterLobby::player_disconnected(const String &p_player_id) {
	if (is_matchmaking()) {
		return;
	}
	const bool was_host = p_player_id == host();
	member_left(p_player_id);
	if (!was_host || !active || !GLOBAL_GET("lobbies/host_migration")) {
		// Without host migration, the game ended with the host's connection (Godot emitted
		// server_disconnected), and the game decides what to do.
		return;
	}
	PackedStringArray remaining = peer->call("get_player_ids");
	remaining.push_back(_own_member());
	remaining.sort();
	const String new_host = remaining[0];
	peer->call("set_host", new_host);
	for (const String &member : remaining) {
		referee->set_peer_id(member, peer->call("get_peer_id_for_player", member));
	}
	host_is(new_host, true);
	set_tree_peer();
}

void PurrGameCenterLobby::message_received(const String &p_player_id, const String &p_message) {
	const Variant parsed = JSON::parse_string(p_message);
	if (parsed.get_type() != Variant::DICTIONARY) {
		return;
	}
	const Dictionary message = parsed;
	// A player joining with a code learns who hosts from the host itself.
	if (String(message.get("t", "")) == "host") {
		if (joining && host().is_empty()) {
			peer->call("set_host", p_player_id);
			ready_if_host_here();
		}
		return;
	}
	received(p_player_id, message);
}

// GameKit sends to the others: this player's game hears its own message at once.
void PurrGameCenterLobby::_send(const Dictionary &p_message) {
	if (peer.is_null()) {
		return;
	}
	if ((bool)peer->call("has_match")) {
		peer->call("send_message", JSON::stringify(p_message));
	}
	received(_own_member(), p_message);
}

void PurrGameCenterLobby::_drop(const String &p_member) {
	if (peer.is_valid()) {
		peer->disconnect_peer(peer->call("get_peer_id_for_player", p_member));
	}
}

void PurrGameCenterLobby::_report(const String &p_member) {
	WARN_PRINT("Game Center has no way for games to report players. Lobbies.block() keeps them away.");
}

void PurrGameCenterLobby::_close() {
	generation++;
	matchmaking_mode = String();
	joining = false;
	joining_code = String();
	if (peer.is_null()) {
		return;
	}
	peer->call("cancel_matchmaking");
	SceneTree *tree = SceneTree::get_singleton();
	if (tree && tree->get_multiplayer()->get_multiplayer_peer() == peer) {
		tree->get_multiplayer()->set_multiplayer_peer(Ref<MultiplayerPeer>());
	}
	peer->close();
	peer.unref();
}

// Matchmaking.

void PurrGameCenterLobby::matchmake(const String &p_mode, int p_players, const Ref<RefCounted> &p_call) {
	leave();
	new_peer();
	pending_call = p_call;
	matchmaking_mode = p_mode;
	matchmaking_players = CLAMP(p_players, 2, 8);
	matchmaking_started = OS::get_singleton()->get_ticks_msec();
	find_match();
}

void PurrGameCenterLobby::cancel_matchmaking() {
	failed("cancelled", "Matchmaking was cancelled.");
}

int PurrGameCenterLobby::search_level() const {
	return MIN(LAST_SEARCH_LEVEL, (int)((OS::get_singleton()->get_ticks_msec() - matchmaking_started) / (uint64_t)(WIDEN_AFTER * 1000)));
}

void PurrGameCenterLobby::find_match() {
	const int level = search_level();
	const int64_t group = player_group(matchmaking_mode, matchmaking_players, level, own_rating(matchmaking_mode));
	if ((Error)(int)peer->call("find_match", matchmaking_players, matchmaking_players, group) != OK) {
		failed("unavailable", "Sign in to Game Center to find players.");
		return;
	}
	if (level < LAST_SEARCH_LEVEL) {
		after(WIDEN_AFTER, ++generation);
	}
}

// Still waiting: look again in a wider band.
void PurrGameCenterLobby::widen(uint64_t p_generation) {
	if (p_generation != generation || !is_matchmaking() || (bool)peer->call("has_match")) {
		return;
	}
	peer->call("cancel_matchmaking");
	find_match();
}

void PurrGameCenterLobby::begin_match() {
	PackedStringArray everyone = peer->call("get_player_ids");
	for (const String &player : everyone) {
		if (turned_away(player)) {
			// Someone this player blocked: on to another match.
			peer->close();
			new_peer();
			find_match();
			return;
		}
	}
	const String ranked_mode = matchmaking_mode;
	matchmaking_mode = String();
	generation++;
	everyone.push_back(_own_member());
	everyone.sort();
	peer->call("set_host", everyone[0]);
	for (const String &member : everyone) {
		member_joined(member, peer->call("get_peer_id_for_player", member));
	}
	host_is(everyone[0], false);
	set_tree_peer();
	start_ranked(ranked_mode);
	entered(String());
}
