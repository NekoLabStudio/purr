#include "purr_steam_lobby.h"

#include "purr.h"

#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "scene/main/multiplayer_api.h"
#include "scene/main/scene_tree.h"

namespace {

// Purr's lobby chat messages, kept apart from any the game sends itself.
const char *MESSAGE_PREFIX = "purr:";
// Lobby data for matchmaking.
const char *MODE_KEY = "purr_mode";
const char *PLAYERS_KEY = "purr_players";
const char *RATING_KEY = "purr_rating";
const char *OPEN_KEY = "purr_open";
// How often matchmaking looks again, and how long each widening of the search waits.
const double SEARCH_INTERVAL = 3.0;
const uint64_t WIDEN_AFTER_MSEC = 5000;
// The rating range at each widening; the last level takes anyone, anywhere.
const int RATING_RANGES[] = { 100, 200, 400 };
const int LAST_SEARCH_LEVEL = 3;

String id_string(uint64_t p_id) {
	return String::num_uint64(p_id);
}

void set_tree_peer(const Ref<MultiplayerPeer> &p_peer) {
	SceneTree *tree = SceneTree::get_singleton();
	if (tree) {
		tree->get_multiplayer()->set_multiplayer_peer(p_peer);
	}
}

} // namespace

PurrSteamLobby::~PurrSteamLobby() {
	if (steam()) {
		listen(false);
		if (steam()->is_connected(SNAME("lobby_created"), callable_mp(this, &PurrSteamLobby::lobby_created))) {
			steam()->disconnect(SNAME("lobby_created"), callable_mp(this, &PurrSteamLobby::lobby_created));
			steam()->disconnect(SNAME("lobby_joined"), callable_mp(this, &PurrSteamLobby::lobby_joined));
		}
	}
}

void PurrSteamLobby::listen_for_lobbies() {
	if (!steam()->is_connected(SNAME("lobby_created"), callable_mp(this, &PurrSteamLobby::lobby_created))) {
		steam()->connect(SNAME("lobby_created"), callable_mp(this, &PurrSteamLobby::lobby_created));
		steam()->connect(SNAME("lobby_joined"), callable_mp(this, &PurrSteamLobby::lobby_joined));
	}
}

void PurrSteamLobby::create_steam_lobby(int64_t p_type, int p_max_members) {
	listen_for_lobbies();
	creates_pending++;
	steam()->call("createLobby", p_type, p_max_members);
}

void PurrSteamLobby::join_steam_lobby(uint64_t p_lobby_id) {
	listen_for_lobbies();
	joining_id = p_lobby_id;
	steam()->call("joinLobby", p_lobby_id);
}

Object *PurrSteamLobby::steam() const {
	return Purr::get_singleton() ? Purr::get_singleton()->steam() : nullptr;
}

int64_t PurrSteamLobby::constant(const char *p_name) const {
	return Purr::get_singleton()->steam_constant(p_name);
}

uint64_t PurrSteamLobby::own_id() const {
	return (uint64_t)(int64_t)steam()->call("getSteamID");
}

String PurrSteamLobby::_own_member() const {
	return steam() ? id_string(own_id()) : String();
}

PackedStringArray PurrSteamLobby::lobby_members() const {
	PackedStringArray members;
	const int count = steam()->call("getNumLobbyMembers", lobby_id);
	for (int i = 0; i < count; i++) {
		members.push_back(id_string((uint64_t)(int64_t)steam()->call("getLobbyMemberByIndex", lobby_id, i)));
	}
	return members;
}

void PurrSteamLobby::listen(bool p_listen) {
	if (p_listen == listening) {
		return;
	}
	listening = p_listen;
	if (p_listen) {
		steam()->connect(SNAME("lobby_chat_update"), callable_mp(this, &PurrSteamLobby::chat_updated));
		steam()->connect(SNAME("lobby_message"), callable_mp(this, &PurrSteamLobby::message_received));
	} else {
		steam()->disconnect(SNAME("lobby_chat_update"), callable_mp(this, &PurrSteamLobby::chat_updated));
		steam()->disconnect(SNAME("lobby_message"), callable_mp(this, &PurrSteamLobby::message_received));
	}
}

// Lobbies made with a code are friends-only: friends see them, and anyone with the code joins.

void PurrSteamLobby::create(const Ref<RefCounted> &p_call) {
	leave();
	pending_call = p_call;
	create_steam_lobby(constant("LOBBY_TYPE_FRIENDS_ONLY"), GLOBAL_GET("lobbies/max_players"));
}

void PurrSteamLobby::join(const String &p_code, const Ref<RefCounted> &p_call) {
	leave();
	const uint64_t id = p_code.to_int();
	if (id == 0) {
		if (p_call.is_valid()) {
			purr_complete(p_call, purr_failure("not_found", "That isn't a Steam lobby's code."));
		}
		return;
	}
	pending_call = p_call;
	join_steam_lobby(id);
}

void PurrSteamLobby::lobby_created(int64_t p_connect, int64_t p_lobby_id) {
	if (creates_pending == 0) {
		return; // The game's own.
	}
	creates_pending--;
	if (creates_abandoned > 0) {
		creates_abandoned--;
		if (p_connect == constant("RESULT_OK")) {
			steam()->call("leaveLobby", p_lobby_id);
		}
		return;
	}
	if (p_connect != constant("RESULT_OK")) {
		if (is_matchmaking()) {
			resume_matchmaking();
			return;
		}
		failed("lobby_failed", vformat("Steam couldn't create a lobby (result %d).", p_connect));
		return;
	}
	lobby_id = p_lobby_id;
	if (is_matchmaking()) {
		own_matchmaking_lobby = true;
		steam()->call("setLobbyData", lobby_id, MODE_KEY, matchmaking_mode);
		steam()->call("setLobbyData", lobby_id, PLAYERS_KEY, itos(matchmaking_players));
		steam()->call("setLobbyData", lobby_id, RATING_KEY, itos((int64_t)Math::round(own_rating(matchmaking_mode))));
		steam()->call("setLobbyData", lobby_id, OPEN_KEY, "1");
		matchmaking_lobby_entered();
		return;
	}
	if (!start_session()) {
		return;
	}
	entered(id_string(lobby_id));
	Purr::get_singleton()->set_lobby_presence(code, false);
}

void PurrSteamLobby::lobby_joined(int64_t p_lobby_id, int64_t p_permissions, bool p_locked, int64_t p_response) {
	const bool success = p_response == constant("CHAT_ROOM_ENTER_RESPONSE_SUCCESS");
	if (joins_abandoned.has((uint64_t)p_lobby_id)) {
		joins_abandoned.erase((uint64_t)p_lobby_id);
		if (success) {
			steam()->call("leaveLobby", p_lobby_id);
		}
		return;
	}
	// The game's own join, or Steam entering a lobby Purr just created (lobby_created has it).
	if (joining_id == 0 || (uint64_t)p_lobby_id != joining_id) {
		return;
	}
	joining_id = 0;
	if (!success) {
		if (is_matchmaking()) {
			resume_matchmaking();
			return;
		}
		failed("lobby_failed", vformat("Couldn't join the lobby (response %d).", p_response));
		return;
	}
	lobby_id = p_lobby_id;
	if (is_matchmaking()) {
		matchmaking_lobby_entered();
		return;
	}
	if (!start_session()) {
		return;
	}
	entered(id_string(lobby_id));
	Purr::get_singleton()->set_lobby_presence(code, false);
}

// In the lobby, playing: everyone in it is a member, the owner hosts, and Purr's messages count.
// False if the peer couldn't start, and the lobby was left.
bool PurrSteamLobby::start_session() {
	listen(true);
	owner = (uint64_t)(int64_t)steam()->call("getLobbyOwner", lobby_id);
	for (const String &member : lobby_members()) {
		member_joined(member, 0);
	}
	host_is(id_string(owner), false);
	return start_peer(owner == own_id(), 0);
}

// The host hosts the lobby's peer, the others connect to it. A player reconnecting to a new host
// keeps their peer ID (p_peer_id), so what they own in the game stays theirs.
// False if it couldn't start, and the lobby was left.
bool PurrSteamLobby::start_peer(bool p_host, int64_t p_peer_id) {
	const Ref<MultiplayerPeer> old_peer = peer;
	if (peer.is_valid()) {
		peer->disconnect(SNAME("peer_connected"), callable_mp(this, &PurrSteamLobby::peer_connected));
		peer->close();
	}
	peer = Ref<MultiplayerPeer>(Object::cast_to<MultiplayerPeer>(ClassDB::instantiate("SteamMultiplayerPeer")));
	const Error error = peer.is_null() ? ERR_UNAVAILABLE : p_host ? (Error)(int)peer->call("host_with_lobby", lobby_id) : (Error)(int)peer->call("connect_to_lobby", lobby_id, p_peer_id);
	if (error != OK) {
		peer.unref();
		SceneTree *tree = SceneTree::get_singleton();
		if (old_peer.is_valid() && tree && tree->get_multiplayer()->get_multiplayer_peer() == old_peer) {
			tree->get_multiplayer()->set_multiplayer_peer(Ref<MultiplayerPeer>());
		}
		failed("lobby_failed", "Couldn't start playing in the Steam lobby.");
		return false;
	}
	peer->connect(SNAME("peer_connected"), callable_mp(this, &PurrSteamLobby::peer_connected));
	referee->set_peer_id(_own_member(), peer->get_unique_id());
	set_tree_peer(peer);
	return true;
}

void PurrSteamLobby::peer_connected(int64_t p_peer_id) {
	const String member = id_string((uint64_t)(int64_t)peer->call("get_steam_id_for_peer_id", p_peer_id));
	referee->set_peer_id(member, p_peer_id);
	// A removed player who connects again is dropped again, and the host tells them again (they
	// may not have been in the lobby yet when it first did).
	if (referee->is_kicked(member)) {
		_drop(member);
		if (is_host()) {
			_send(PurrLobbyReferee::make_kick(member));
		}
	}
}

void PurrSteamLobby::chat_updated(int64_t p_lobby_id, int64_t p_changed_id, int64_t p_making_change_id, int64_t p_state) {
	if ((uint64_t)p_lobby_id != lobby_id) {
		return;
	}
	const String member = id_string((uint64_t)p_changed_id);
	if (p_state & constant("CHAT_MEMBER_STATE_CHANGE_ENTERED")) {
		if (is_matchmaking()) {
			if ((int)steam()->call("getNumLobbyMembers", lobby_id) >= matchmaking_players) {
				begin_match();
			}
			return;
		}
		member_joined(member, 0);
		return;
	}
	if (is_matchmaking()) {
		return;
	}
	member_left(member);
	if ((uint64_t)p_changed_id == owner) {
		owner_left();
	}
}

// Steam hands the lobby to another member. With host migration, they become peer 1 and everyone
// reconnects to them; without it, the game ended with the host's connection (Godot emitted
// server_disconnected), and the game decides what to do.
void PurrSteamLobby::owner_left() {
	owner = (uint64_t)(int64_t)steam()->call("getLobbyOwner", lobby_id);
	if (!GLOBAL_GET("lobbies/host_migration") || owner == 0) {
		return;
	}
	const bool you = owner == own_id();
	const int64_t own_peer_id = peer.is_valid() ? peer->get_unique_id() : 0;
	host_is(id_string(owner), true);
	start_peer(you, you ? 0 : own_peer_id);
}

void PurrSteamLobby::message_received(int64_t p_lobby_id, int64_t p_user, const String &p_message, int64_t p_type) {
	if ((uint64_t)p_lobby_id != lobby_id || !p_message.begins_with(MESSAGE_PREFIX)) {
		return;
	}
	const Variant message = JSON::parse_string(p_message.substr(strlen(MESSAGE_PREFIX)));
	if (message.get_type() == Variant::DICTIONARY) {
		received(id_string((uint64_t)p_user), message);
	}
}

void PurrSteamLobby::_send(const Dictionary &p_message) {
	if (lobby_id != 0) {
		steam()->call("sendLobbyChatMsg", lobby_id, String(MESSAGE_PREFIX) + JSON::stringify(p_message));
	}
}

void PurrSteamLobby::_drop(const String &p_member) {
	if (peer.is_null()) {
		return;
	}
	const int64_t peer_id = peer->call("get_peer_id_for_steam_id", (uint64_t)p_member.to_int());
	if (peer_id > 0) {
		peer->disconnect_peer(peer_id);
	}
}

// Steam's overlay on the player's profile, where Steam's own report button is.
void PurrSteamLobby::_report(const String &p_member) {
	steam()->call("activateGameOverlayToUser", "steamid", (uint64_t)p_member.to_int());
}

void PurrSteamLobby::_close() {
	generation++;
	matchmaking_mode = String();
	own_matchmaking_lobby = false;
	creates_abandoned = creates_pending;
	if (joining_id != 0) {
		joins_abandoned.insert(joining_id);
		joining_id = 0;
	}
	if (!steam()) {
		return;
	}
	listen(false);
	if (lobby_id != 0) {
		if (Purr::get_singleton()) {
			Purr::get_singleton()->set_lobby_presence(String(), false);
		}
		steam()->call("leaveLobby", lobby_id);
	}
	lobby_id = 0;
	owner = 0;
	if (peer.is_valid()) {
		peer->disconnect(SNAME("peer_connected"), callable_mp(this, &PurrSteamLobby::peer_connected));
		SceneTree *tree = SceneTree::get_singleton();
		if (tree && tree->get_multiplayer()->get_multiplayer_peer() == peer) {
			tree->get_multiplayer()->set_multiplayer_peer(Ref<MultiplayerPeer>());
		}
		peer->close();
		peer.unref();
	}
}

// Matchmaking. Looks for an open lobby of the same mode and size, with a rating close to the
// player's; finding none, opens one and waits there, still looking. Two players who each opened
// one meet in the one with the lower ID. Once a lobby has everyone, it closes and the match begins.

void PurrSteamLobby::matchmake(const String &p_mode, int p_players, const Ref<RefCounted> &p_call) {
	leave();
	pending_call = p_call;
	matchmaking_mode = p_mode;
	matchmaking_players = CLAMP(p_players, 2, 8);
	matchmaking_started = OS::get_singleton()->get_ticks_msec();
	search();
}

void PurrSteamLobby::cancel_matchmaking() {
	failed("cancelled", "Matchmaking was cancelled.");
}

int PurrSteamLobby::search_level() const {
	return MIN(LAST_SEARCH_LEVEL, (int)((OS::get_singleton()->get_ticks_msec() - matchmaking_started) / WIDEN_AFTER_MSEC));
}

void PurrSteamLobby::search() {
	const uint64_t this_search = ++generation;
	const int level = search_level();
	steam()->call("addRequestLobbyListStringFilter", MODE_KEY, matchmaking_mode, constant("LOBBY_COMPARISON_EQUAL"));
	steam()->call("addRequestLobbyListStringFilter", OPEN_KEY, "1", constant("LOBBY_COMPARISON_EQUAL"));
	steam()->call("addRequestLobbyListNumericalFilter", PLAYERS_KEY, matchmaking_players, constant("LOBBY_COMPARISON_EQUAL"));
	steam()->call("addRequestLobbyListFilterSlotsAvailable", 1);
	if (level < LAST_SEARCH_LEVEL) {
		const int64_t rating = Math::round(own_rating(matchmaking_mode));
		steam()->call("addRequestLobbyListNumericalFilter", RATING_KEY, rating - RATING_RANGES[level], constant("LOBBY_COMPARISON_EQUAL_TO_GREATER_THAN"));
		steam()->call("addRequestLobbyListNumericalFilter", RATING_KEY, rating + RATING_RANGES[level], constant("LOBBY_COMPARISON_EQUAL_TO_OR_LESS_THAN"));
	}
	steam()->call("addRequestLobbyListDistanceFilter", constant(level == 0 ? "LOBBY_DISTANCE_FILTER_DEFAULT" : level < LAST_SEARCH_LEVEL ? "LOBBY_DISTANCE_FILTER_FAR" : "LOBBY_DISTANCE_FILTER_WORLDWIDE"));
	steam()->call("addRequestLobbyListResultCountFilter", 20);
	steam()->connect(SNAME("lobby_match_list"), callable_mp(this, &PurrSteamLobby::lobbies_found).bind(this_search), CONNECT_ONE_SHOT);
	steam()->call("requestLobbyList");
}

void PurrSteamLobby::search_later(uint64_t p_generation) {
	SceneTree *tree = SceneTree::get_singleton();
	ERR_FAIL_NULL(tree);
	tree->create_timer(SEARCH_INTERVAL)->connect(SNAME("timeout"), callable_mp(this, &PurrSteamLobby::search_again).bind(p_generation), CONNECT_ONE_SHOT);
}

// Unless matchmaking stopped or moved on meanwhile.
void PurrSteamLobby::search_again(uint64_t p_generation) {
	if (p_generation == generation && is_matchmaking()) {
		search();
	}
}

void PurrSteamLobby::lobbies_found(const Array &p_lobbies, uint64_t p_generation) {
	if (p_generation != generation || !is_matchmaking()) {
		return;
	}
	// Only open lobbies of this mode (the list could be the game's own search).
	uint64_t best = 0;
	for (const Variant &entry : p_lobbies) {
		const uint64_t id = (uint64_t)(int64_t)entry;
		const bool matching = String(steam()->call("getLobbyData", id, MODE_KEY)) == matchmaking_mode && String(steam()->call("getLobbyData", id, OPEN_KEY)) == "1";
		if (matching && id != lobby_id && (best == 0 || id < best)) {
			best = id;
		}
	}
	if (own_matchmaking_lobby) {
		// Waiting alone in our own: move to another player's if it's the one we'd both pick.
		if (best != 0 && best < lobby_id && (int)steam()->call("getNumLobbyMembers", lobby_id) <= 1) {
			steam()->call("leaveLobby", lobby_id);
			lobby_id = 0;
			own_matchmaking_lobby = false;
			listen(false);
			join_steam_lobby(best);
			return;
		}
		search_later(generation);
		return;
	}
	if (best != 0) {
		join_steam_lobby(best);
		return;
	}
	create_steam_lobby(constant("LOBBY_TYPE_PUBLIC"), matchmaking_players);
}

// In a matchmaking lobby: one that's no longer open, or has a player this one blocked, is left
// for another; one that's full begins the match.
void PurrSteamLobby::matchmaking_lobby_entered() {
	listen(true);
	bool usable = String(steam()->call("getLobbyData", lobby_id, OPEN_KEY)) == "1" && String(steam()->call("getLobbyData", lobby_id, MODE_KEY)) == matchmaking_mode;
	for (const String &member : lobby_members()) {
		usable = usable && !turned_away(member);
	}
	if (!usable) {
		resume_matchmaking();
		return;
	}
	if ((int)steam()->call("getNumLobbyMembers", lobby_id) >= matchmaking_players) {
		begin_match();
	} else if (own_matchmaking_lobby) {
		search_later(generation);
	}
}

void PurrSteamLobby::begin_match() {
	for (const String &member : lobby_members()) {
		if (turned_away(member)) {
			resume_matchmaking();
			return;
		}
	}
	const String ranked_mode = matchmaking_mode;
	matchmaking_mode = String();
	generation++;
	if ((uint64_t)(int64_t)steam()->call("getLobbyOwner", lobby_id) == own_id()) {
		steam()->call("setLobbyData", lobby_id, OPEN_KEY, "0");
		steam()->call("setLobbyJoinable", lobby_id, false);
	}
	if (!start_session()) {
		return;
	}
	start_ranked(ranked_mode);
	entered(id_string(lobby_id));
}

// Back to looking, after a lobby that didn't work out.
void PurrSteamLobby::resume_matchmaking() {
	listen(false);
	if (lobby_id != 0) {
		steam()->call("leaveLobby", lobby_id);
	}
	lobby_id = 0;
	own_matchmaking_lobby = false;
	search_later(++generation);
}
