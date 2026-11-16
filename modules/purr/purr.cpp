#include "purr.h"

#include "account.h"
#include "achievements.h"
#include "local_players.h"
#include "lobbies.h"
#include "purr_credentials.h"
#include "saves.h"

#include "modules/purr_online/purr_online.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/variant/variant_utility.h"
#include "scene/main/multiplayer_api.h"
#include "scene/main/scene_tree.h"

Purr *Purr::singleton = nullptr;

namespace {

const char *REMEMBERED_KEY = "remembered_guests";
const char *DEVICE_SECRET_KEY = "device_secret";
const char *LAST_ACCOUNT_KEY = "last_account_id";
// Whether that account has an email (so its saves are on the service), for signing in offline.
const char *LAST_ACCOUNT_EMAIL_KEY = "last_account_has_email";
// The name the player chose, for signing in offline or without the online service.
const char *CHOSEN_NAME_KEY = "chosen_display_name";

Ref<PurrOnlineCall> new_call() {
	Ref<PurrOnlineCall> call;
	call.instantiate();
	return call;
}

Signal signal_of(const Ref<RefCounted> &p_call) {
	return Signal(p_call.ptr(), SNAME("completed"));
}

void emit_completed(const Ref<RefCounted> &p_call, const Variant &p_result) {
	p_call->emit_signal(SNAME("completed"), p_result);
}

} // namespace

// Deferred, so a caller awaiting the signal is connected before it fires. The deferred call holds
// the call object, which nothing else may be keeping alive by then.
void purr_complete(const Ref<RefCounted> &p_call, const Variant &p_result) {
	callable_mp_static(&emit_completed).call_deferred(p_call, p_result);
}

namespace {

void complete(const Ref<RefCounted> &p_call, const Variant &p_result) {
	purr_complete(p_call, p_result);
}

Dictionary failure(const String &p_error, const String &p_message) {
	Dictionary result;
	result["ok"] = false;
	result["error"] = p_error;
	result["message"] = p_message;
	return result;
}

} // namespace

Purr::Purr() {
	singleton = this;
	lobby.instantiate();
	friends.instantiate();
	achievements.instantiate();
	leaderboards.instantiate();
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::STRING, "saves/backend", PROPERTY_HINT_ENUM, "automatic,purr_online,platform,local"), "automatic");
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::INT, "lobbies/max_players", PROPERTY_HINT_RANGE, "2,8"), 4);
	// Crossplay lobbies connect only through TURN, so players never see each other's IP address.
	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "lobbies/relay_only"), false);
	// When the host leaves a crossplay lobby, the player who's been there longest takes over.
	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "lobbies/host_migration"), false);
	// Google Play Games on Android (the play_games module, which isn't in the editor, so its
	// settings are here): the game's application ID from the Play Console, the OAuth web client ID
	// Purr's online service exchanges sign-in codes with, and whether saves go to Play Games'
	// saved games (turned on for the game in the Play Console).
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::STRING, "play_games/app_id", PROPERTY_HINT_PLACEHOLDER_TEXT, "123456789012"), "");
	GLOBAL_DEF(PropertyInfo(Variant::STRING, "play_games/server_client_id", PROPERTY_HINT_PLACEHOLDER_TEXT, "1234-abcd.apps.googleusercontent.com"), "");
	GLOBAL_DEF(PropertyInfo(Variant::BOOL, "play_games/saved_games"), false);
	// A game activity (set up in App Store Connect, with party codes) that crossplay lobbies start,
	// so friends see the party in Apple's Games app and join it from there.
	GLOBAL_DEF(PropertyInfo(Variant::STRING, "game_center/party_activity", PROPERTY_HINT_PLACEHOLDER_TEXT, "Game activity ID"), "");
	// The online service turned this game's version away, or is too old for this Purr: passed on.
	if (PurrOnline::get_singleton()) {
		PurrOnline::get_singleton()->connect(SNAME("update_required"), callable_mp(this, &Purr::online_update_required));
		PurrOnline::get_singleton()->connect(SNAME("service_outdated"), callable_mp(this, &Purr::online_service_outdated));
	}
}

Purr::~Purr() {
	singleton = nullptr;
}

Object *Purr::steam() const {
	return Engine::get_singleton()->has_singleton("Steam") ? Engine::get_singleton()->get_singleton_object("Steam") : nullptr;
}

Object *Purr::game_center() const {
	return Engine::get_singleton()->has_singleton("GameCenter") ? Engine::get_singleton()->get_singleton_object("GameCenter") : nullptr;
}

bool Purr::game_center_ready() const {
	return game_center() && (bool)game_center()->call("is_authenticated");
}

Object *Purr::play_games() const {
	return Engine::get_singleton()->has_singleton("PlayGames") ? Engine::get_singleton()->get_singleton_object("PlayGames") : nullptr;
}

bool Purr::play_games_ready() const {
	return play_games() && (bool)play_games()->call("is_authenticated");
}

// The player's name on the platform they're signed in with, if any.
String Purr::platform_name() const {
	if (steam_ready()) {
		return steam()->call("getPersonaName");
	}
	if (game_center_ready()) {
		return game_center()->call("get_player_name");
	}
	if (play_games_ready()) {
		return play_games()->call("get_player_name");
	}
	return String();
}

int64_t Purr::steam_constant(const String &p_name) const {
	return ClassDB::get_integer_constant("Steam", p_name);
}

bool Purr::steam_ready() const {
	if (!steam()) {
		return false;
	}
	const Dictionary started = steam()->call("get_steam_init_result");
	return (int64_t)started.get("status", -1) == steam_constant("STEAM_API_INIT_RESULT_OK");
}

// The online service keeps a player's saves once they've added an email (p_online); until then,
// the platform's cloud does, or this device where there's none.
PlayerSaves::Backend Purr::save_backend(bool p_online) const {
	const String setting = GLOBAL_GET("saves/backend");
	if ((setting == "online" || setting == "automatic") && p_online) {
		return PlayerSaves::BACKEND_ONLINE;
	}
	if (setting == "online") {
		return PlayerSaves::BACKEND_LOCAL;
	}
	const bool steam_cloud = steam_ready() && (bool)steam()->call("isCloudEnabledForApp") && (bool)steam()->call("isCloudEnabledForAccount");
	if ((setting == "platform" || setting == "automatic") && (steam_cloud || cloud_store())) {
		return PlayerSaves::BACKEND_PLATFORM;
	}
	return PlayerSaves::BACKEND_LOCAL;
}

// iCloud through CloudKit (the icloud module), when the game set its container and the player is
// signed in to iCloud; or Play Games' saved games, when the game turned them on and the player is
// signed in. Not where Steam runs: there, Steam Cloud is the platform's cloud.
Object *Purr::cloud_store() const {
	if (steam_ready()) {
		return nullptr;
	}
	Object *icloud = Engine::get_singleton()->has_singleton("ICloud") ? Engine::get_singleton()->get_singleton_object("ICloud") : nullptr;
	if (icloud && (bool)icloud->call("is_available")) {
		return icloud;
	}
	if (play_games() && (bool)play_games()->call("is_saved_games_enabled")) {
		return play_games();
	}
	return nullptr;
}

String Purr::get_device_label() const {
	return device_label.is_empty() ? OS::get_singleton()->get_model_name() : device_label;
}

// Signing in.

Signal Purr::sign_in() {
	// Two at once would make two device accounts (each with its own secret, one lost): a sign-in
	// asked for while one is under way gets that one's result.
	if (sign_in_call.is_valid()) {
		return signal_of(sign_in_call);
	}
	const Ref<PurrOnlineCall> call = new_call();
	sign_in_call = call;
	PurrOnline *online = PurrOnline::get_singleton();
	// Before Game Center signs in, which is when it hands over an invite the game was opened with.
	friends->start();

	// Steam, if the game was started from it (initialised here unless the game already did).
	if (steam() && !steam_ready()) {
		const int64_t app_id = GLOBAL_GET("steam/initialization/app_data/app_id");
		steam()->call("steamInitEx", app_id, true);
	}
	if (steam_ready()) {
		provider = "steam";
		if (online->is_configured()) {
			sign_in_steam_online(call);
		} else {
			finish_sign_in(false, steam()->call("getSteamID").operator String(), steam()->call("getPersonaName"), call);
		}
		return signal_of(call);
	}

	// Game Center on Apple platforms.
	if (game_center()) {
		Signal authenticated = game_center()->call("authenticate");
		authenticated.connect(callable_mp(this, &Purr::game_center_authenticated).bind(call), CONNECT_ONE_SHOT);
		return signal_of(call);
	}

	// Google Play Games on Android, when the game set its application ID.
	if (play_games() && (bool)play_games()->call("is_available")) {
		Signal authenticated = play_games()->call("authenticate", true);
		authenticated.connect(callable_mp(this, &Purr::play_games_authenticated).bind(call), CONNECT_ONE_SHOT);
		return signal_of(call);
	}

	sign_in_without_platform(call);
	return signal_of(call);
}

// No platform sign-in: a device account if the online service is set up, else this device alone.
void Purr::sign_in_without_platform(const Ref<RefCounted> &p_call) {
	const Ref<RefCounted> call = p_call;
	PurrOnline *online = PurrOnline::get_singleton();
	provider = online->is_configured() ? "device" : "local";
	if (online->is_configured()) {
		String secret = PurrCredentials::load(DEVICE_SECRET_KEY);
		if (secret.is_empty()) {
			uint8_t bytes[32];
			if (OS::get_singleton()->get_entropy(bytes, sizeof(bytes)) != OK) {
				ERR_PRINT("No random bytes for this device's account: playing offline.");
				finish_sign_in(false, String(), String(), call);
				return;
			}
			secret = String::hex_encode_buffer(bytes, sizeof(bytes));
			PurrCredentials::store(DEVICE_SECRET_KEY, secret);
		}
		Signal signed_in = online->sign_in_device(secret);
		signed_in.connect(callable_mp(this, &Purr::online_signed_in).bind(call), CONNECT_ONE_SHOT);
	} else {
		finish_sign_in(false, String(), String(), call);
	}
}

void Purr::game_center_authenticated(const Dictionary &p_result, const Ref<RefCounted> &p_call) {
	if (!p_result.get("ok", false)) {
		// Not signed in to Game Center: play on with a device account or this device alone.
		sign_in_without_platform(p_call);
		return;
	}
	provider = "game_center";
	if (!PurrOnline::get_singleton()->is_configured()) {
		finish_sign_in(false, game_center()->call("get_team_player_id").operator String(), p_result.get("player_name", ""), p_call);
		return;
	}
	Signal verified = game_center()->call("fetch_identity_verification");
	verified.connect(callable_mp(this, &Purr::game_center_verified).bind(p_call), CONNECT_ONE_SHOT);
}

void Purr::game_center_verified(const Dictionary &p_result, const Ref<RefCounted> &p_call) {
	const String name = game_center()->call("get_player_name");
	if (!p_result.get("ok", false)) {
		finish_sign_in(false, PurrCredentials::load(LAST_ACCOUNT_KEY), name, p_call);
		return;
	}
	Dictionary proof = p_result.duplicate();
	proof.erase("ok");
	proof.erase("error");
	Signal signed_in = PurrOnline::get_singleton()->sign_in_game_center(proof, name);
	signed_in.connect(callable_mp(this, &Purr::online_signed_in).bind(p_call), CONNECT_ONE_SHOT);
}

void Purr::play_games_authenticated(const Dictionary &p_result, const Ref<RefCounted> &p_call) {
	if (!p_result.get("ok", false)) {
		// Not signed in to Play Games: play on with a device account or this device alone.
		sign_in_without_platform(p_call);
		return;
	}
	provider = "play_games";
	if (!PurrOnline::get_singleton()->is_configured()) {
		finish_sign_in(false, p_result.get("player_id", ""), p_result.get("player_name", ""), p_call);
		return;
	}
	Signal code = play_games()->call("request_server_auth_code");
	code.connect(callable_mp(this, &Purr::play_games_code_received).bind(p_call), CONNECT_ONE_SHOT);
}

void Purr::play_games_code_received(const Dictionary &p_result, const Ref<RefCounted> &p_call) {
	const String name = play_games()->call("get_player_name");
	if (!p_result.get("ok", false)) {
		// Google couldn't vouch for the player right now: play on, synced later.
		finish_sign_in(false, PurrCredentials::load(LAST_ACCOUNT_KEY), name, p_call);
		return;
	}
	Signal signed_in = PurrOnline::get_singleton()->sign_in_play_games(p_result.get("code", ""), name);
	signed_in.connect(callable_mp(this, &Purr::online_signed_in).bind(p_call), CONNECT_ONE_SHOT);
}

void Purr::sign_in_steam_online(const Ref<RefCounted> &p_call) {
	steam()->connect(SNAME("get_ticket_for_web_api"), callable_mp(this, &Purr::steam_ticket).bind(p_call), CONNECT_ONE_SHOT);
	steam()->call("getAuthTicketForWebApi", "purr-online");
}

void Purr::steam_ticket(int64_t p_auth_ticket, int64_t p_result, int64_t p_size, const PackedByteArray &p_ticket, const Ref<RefCounted> &p_call) {
	const String name = steam()->call("getPersonaName");
	if (p_result != steam_constant("RESULT_OK")) {
		// Steam couldn't vouch for the player right now: play on, synced later.
		finish_sign_in(false, PurrCredentials::load(LAST_ACCOUNT_KEY), name, p_call);
		return;
	}
	Signal signed_in = PurrOnline::get_singleton()->sign_in_steam(String::hex_encode_buffer(p_ticket.ptr(), p_size), name);
	signed_in.connect(callable_mp(this, &Purr::online_signed_in).bind(p_call), CONNECT_ONE_SHOT);
}

void Purr::online_signed_in(const Dictionary &p_result, const Ref<RefCounted> &p_call) {
	const String steam_name = platform_name();
	if (!p_result.get("ok", false)) {
		// Offline, or the service is down: the game goes on with this device's copy of the
		// last account's saves, which sync the next time it signs in.
		finish_sign_in(false, PurrCredentials::load(LAST_ACCOUNT_KEY), steam_name, p_call);
		return;
	}
	const Dictionary data = p_result["data"];
	PurrCredentials::store(LAST_ACCOUNT_KEY, data["account_id"]);
	email_added = Array(data.get("providers", Array())).has("email");
	PurrCredentials::store(LAST_ACCOUNT_EMAIL_KEY, email_added ? "1" : "");
	// A name the player chose wins over the platform's.
	name_chosen = data.get("name_chosen", false);
	// Not chosen (or reset by the service): this device forgets the old one too.
	if (name_chosen) {
		PurrCredentials::store(CHOSEN_NAME_KEY, data.get("display_name", ""));
	} else {
		PurrCredentials::erase(CHOSEN_NAME_KEY);
	}
	const String name = name_chosen || steam_name.is_empty() ? String(data.get("display_name", "")) : steam_name;
	finish_sign_in(true, data["account_id"], name, p_call);
}

void Purr::finish_sign_in(bool p_online, const String &p_account_id, const String &p_name, const Ref<RefCounted> &p_call) {
	const bool configured = PurrOnline::get_singleton()->is_configured();
	if (!p_online) {
		// Offline: the last account's saves, which sync when it next signs in.
		email_added = configured && !p_account_id.is_empty() && p_account_id == PurrCredentials::load(LAST_ACCOUNT_KEY) && PurrCredentials::load(LAST_ACCOUNT_EMAIL_KEY) == "1";
	}
	const String chosen = PurrCredentials::load(CHOSEN_NAME_KEY);
	if (!p_online) {
		name_chosen = !chosen.is_empty();
	}
	String name = p_name;
	if (name.is_empty() || (!p_online && !chosen.is_empty() && (provider == "device" || provider == "local"))) {
		name = chosen.is_empty() ? String("Player") : chosen;
	}
	player.instantiate();
	player->setup(LocalPlayer::KIND_PLAYER, name, p_account_id, String(), false, save_backend(configured && email_added));
	local_players.clear();
	local_players.push_back(player);
	attach_saves();
	player->get_saves()->sync();
	achievements->start();
	if (steam_ready() && !steam()->is_connected(SNAME("join_requested"), callable_mp(this, &Purr::steam_join_requested))) {
		steam()->connect(SNAME("join_requested"), callable_mp(this, &Purr::steam_join_requested));
		steam()->connect(SNAME("join_game_requested"), callable_mp(this, &Purr::steam_join_game_requested));
	}
	load_remembered();

	Dictionary result;
	result["ok"] = true;
	result["provider"] = Account::provider_from(provider);
	result["player_name"] = player->get_player_name();
	result["online"] = p_online;
	sign_in_call.unref();
	complete(p_call, result);
	Account::get_singleton()->call_deferred(SNAME("emit_signal"), SNAME("signed_in"));

	// Started from a friend's invite or "Join Game" (Steam passes it on the command line), or an
	// invite accepted while signing in.
	const List<String> arguments = command_line_read ? List<String>() : OS::get_singleton()->get_cmdline_args();
	command_line_read = true;
	for (const List<String>::Element *argument = arguments.front(); argument && argument->next(); argument = argument->next()) {
		if (argument->get() == PURR_INVITE_ARGUMENT) {
			invited_code = argument->next()->get();
		} else if (argument->get() == STEAM_LOBBY_ARGUMENT && steam_ready()) {
			join_steam_lobby(argument->next()->get().to_int());
		}
	}
	if (!invited_code.is_empty()) {
		join_invited(invited_code);
	}
}

void Purr::attach_saves() {
	const Ref<PlayerSaves> saves = player->get_saves();
	saves->connect(SNAME("synced"), callable_mp(this, &Purr::save_synced));
	saves->connect(SNAME("conflict"), callable_mp(this, &Purr::save_conflicted));
}

void Purr::save_synced(const String &p_slot) {
	// Achievements earned on another device or platform, or as someone else's guest: the
	// platform gets them here.
	if (p_slot == PURR_ACHIEVEMENTS_SLOT) {
		achievements->send_missing();
		Achievements::get_singleton()->emit_signal(SNAME("changed"));
		return;
	}
	Saves::get_singleton()->emit_signal(SNAME("synced"), p_slot);
}

void Purr::save_conflicted(const String &p_slot, const Dictionary &p_local_summary, const Dictionary &p_remote_summary) {
	Saves::get_singleton()->emit_signal(SNAME("conflict"), p_slot, p_local_summary, p_remote_summary);
}

String Purr::get_player_name() const {
	return player.is_valid() ? player->get_player_name() : String();
}

bool Purr::is_available(const String &p_feature) const {
	const bool online = PurrOnline::get_singleton()->is_configured() && !PurrOnline::get_singleton()->get_token().is_empty() && PurrOnline::get_singleton()->get_blocked_reason().is_empty();
	if (p_feature == "sign_in" || p_feature == "saves") {
		return true;
	}
	if (p_feature == "cloud_saves") {
		return player.is_valid() && player->get_saves()->get_backend() != PlayerSaves::BACKEND_LOCAL;
	}
	if (p_feature == "achievements" || p_feature == "leaderboards") {
		return steam_ready() || game_center_ready() || play_games_ready();
	}
	if (p_feature == "lobbies") {
		return online || steam_ready();
	}
	if (p_feature == "invites") {
		return friends->can_invite();
	}
	if (p_feature == "friends") {
		return steam_ready();
	}
	if (p_feature == "local_profiles") {
		return true;
	}
	if (p_feature == "guests" || p_feature == "linking" || p_feature == "crossplay" || p_feature == "matchmaking") {
		return online;
	}
	return false;
}

Signal Purr::deferred_result(const Variant &p_result) {
	const Ref<PurrOnlineCall> call = new_call();
	complete(call, p_result);
	return signal_of(call);
}

// Achievements and leaderboards: see PurrAchievements and PurrLeaderboards.

void Purr::unlock_achievement(const String &p_id) {
	achievements->report(p_id, 1, 1);
}

// Progress towards an achievement, unlocked once p_current reaches p_total. It only ever goes up.
void Purr::set_achievement_progress(const String &p_id, int64_t p_current, int64_t p_total) {
	achievements->report(p_id, p_current, p_total);
}

// {id: progress from 0 to 1}: everything the player has made progress on, wherever they did.
Dictionary Purr::get_achievements() const {
	return player.is_valid() ? PurrAchievements::read(player->get_saves()) : Dictionary();
}

void Purr::show_achievements() {
	achievements->show();
}

void Purr::submit_score(const String &p_leaderboard, int64_t p_score) {
	leaderboards->submit(p_leaderboard, p_score);
}

Signal Purr::get_scores(const String &p_leaderboard, const String &p_scope, int p_count) {
	const Ref<PurrOnlineCall> call = new_call();
	leaderboards->load_scores(p_leaderboard, p_scope, p_count, call);
	return signal_of(call);
}

void Purr::show_leaderboard(const String &p_leaderboard) {
	leaderboards->show(p_leaderboard);
}

// Lobbies: crossplay ones through the online service when it's set up (see PurrLobby), otherwise
// Steam's, where the code is the Steam lobby's ID and the game plays through Godot's high-level
// multiplayer with a SteamMultiplayerPeer.

bool Purr::online_lobbies() const {
	return PurrOnline::get_singleton()->is_configured() && !PurrOnline::get_singleton()->get_token().is_empty() && PurrOnline::get_singleton()->get_blocked_reason().is_empty();
}

Signal Purr::lobby_create() {
	const Ref<PurrOnlineCall> call = new_call();
	if (online_lobbies()) {
		lobby->create(call);
		return signal_of(call);
	}
	if (!steam_ready()) {
		complete(call, failure("unavailable", "Lobbies need Steam on this platform for now."));
		return signal_of(call);
	}
	pending_lobby_call = call;
	steam()->connect(SNAME("lobby_created"), callable_mp(this, &Purr::steam_lobby_created), CONNECT_ONE_SHOT);
	steam()->call("createLobby", steam_constant("LOBBY_TYPE_FRIENDS_ONLY"), (int64_t)GLOBAL_GET("lobbies/max_players"));
	return signal_of(call);
}

void Purr::steam_lobby_created(int64_t p_connect, int64_t p_lobby_id) {
	const Ref<RefCounted> call = pending_lobby_call;
	pending_lobby_call.unref();
	if (p_connect != steam_constant("RESULT_OK")) {
		complete(call, failure("lobby_failed", vformat("Steam couldn't create a lobby (result %d).", p_connect)));
		return;
	}
	steam_lobby = p_lobby_id;
	set_lobby_presence(String::num_uint64(p_lobby_id), false);
	Ref<MultiplayerPeer> peer = Object::cast_to<MultiplayerPeer>(ClassDB::instantiate("SteamMultiplayerPeer"));
	peer->call("host_with_lobby", p_lobby_id);
	SceneTree::get_singleton()->get_multiplayer()->set_multiplayer_peer(peer);
	Dictionary result;
	result["ok"] = true;
	result["code"] = String::num_uint64(p_lobby_id);
	complete(call, result);
}

Signal Purr::lobby_join(const String &p_code) {
	const Ref<PurrOnlineCall> call = new_call();
	if (online_lobbies()) {
		lobby->join(p_code, call);
		return signal_of(call);
	}
	if (!steam_ready()) {
		complete(call, failure("unavailable", "Lobbies need Steam on this platform for now."));
		return signal_of(call);
	}
	pending_lobby_call = call;
	steam()->connect(SNAME("lobby_joined"), callable_mp(this, &Purr::steam_lobby_entered), CONNECT_ONE_SHOT);
	steam()->call("joinLobby", (int64_t)p_code.to_int());
	return signal_of(call);
}

void Purr::steam_lobby_entered(int64_t p_lobby_id, int64_t p_permissions, bool p_locked, int64_t p_response) {
	const Ref<RefCounted> call = pending_lobby_call;
	pending_lobby_call.unref();
	if (p_response != steam_constant("CHAT_ROOM_ENTER_RESPONSE_SUCCESS")) {
		if (call.is_valid()) {
			complete(call, failure("lobby_failed", vformat("Couldn't join the lobby (response %d).", p_response)));
		}
		return;
	}
	steam_lobby = p_lobby_id;
	set_lobby_presence(String::num_uint64(p_lobby_id), false);
	Ref<MultiplayerPeer> peer = Object::cast_to<MultiplayerPeer>(ClassDB::instantiate("SteamMultiplayerPeer"));
	peer->call("connect_to_lobby", p_lobby_id);
	SceneTree::get_singleton()->get_multiplayer()->set_multiplayer_peer(peer);
	Dictionary result;
	result["ok"] = true;
	result["code"] = String::num_uint64(p_lobby_id);
	if (call.is_valid()) {
		complete(call, result);
	}
	Lobbies::get_singleton()->call_deferred(SNAME("emit_signal"), SNAME("joined"), result["code"]);
}

// A friend's invite to a Steam lobby accepted from the Steam overlay: joined at once.
void Purr::steam_join_requested(int64_t p_lobby_id, int64_t p_friend_id) {
	join_steam_lobby(p_lobby_id);
}

void Purr::join_steam_lobby(uint64_t p_lobby_id) {
	if (p_lobby_id == 0 || p_lobby_id == steam_lobby) {
		return;
	}
	lobby_leave();
	steam()->connect(SNAME("lobby_joined"), callable_mp(this, &Purr::steam_lobby_entered), CONNECT_ONE_SHOT);
	steam()->call("joinLobby", p_lobby_id);
}

// A friend's invite accepted, or "Join Game" chosen in Steam's friends list, while the game runs.
void Purr::steam_join_game_requested(int64_t p_friend_id, const String &p_connect) {
	const PackedStringArray parts = p_connect.split(" ", false);
	if (parts.size() != 2) {
		return;
	}
	if (parts[0] == PURR_INVITE_ARGUMENT) {
		join_invited(parts[1]);
	} else if (parts[0] == STEAM_LOBBY_ARGUMENT) {
		join_steam_lobby(parts[1].to_int());
	}
}

// Accepting an invite (on Steam or in Game Center) joins at once, as the player asked; one
// accepted while signing in waits for it to finish.
void Purr::join_invited(const String &p_code) {
	if (player.is_null() || sign_in_call.is_valid()) {
		invited_code = p_code;
		return;
	}
	invited_code = String();
	if (!online_lobbies()) {
		ERR_PRINT("Joining a friend's crossplay lobby needs the online service, signed in.");
		return;
	}
	if (lobby->get_code() != p_code) {
		lobby_join(p_code);
	}
}

void Purr::set_lobby_presence(const String &p_code, bool p_crossplay) {
	friends->set_lobby(p_code, p_crossplay);
}

Array Purr::get_friends() const {
	return friends->get_friends();
}

void Purr::invite_friend(const String &p_friend_id) {
	friends->invite(p_friend_id);
}

void Purr::lobby_leave() {
	set_lobby_presence(String(), false);
	lobby->leave();
	if (steam_lobby != 0 && steam_ready()) {
		steam()->call("leaveLobby", steam_lobby);
	}
	steam_lobby = 0;
	if (SceneTree::get_singleton()) {
		SceneTree::get_singleton()->get_multiplayer()->set_multiplayer_peer(Ref<MultiplayerPeer>());
	}
}

// Crossplay lobbies only: the host removes a player (who can't come back to that lobby); anyone
// reports or blocks a player they're playing with.
void Purr::lobby_kick(int64_t p_peer_id) {
	ERR_FAIL_COND_MSG(!lobby->is_active() || !lobby->is_host(), "Only the host of a crossplay lobby can remove players.");
	PurrOnline::get_singleton()->kick_peer(p_peer_id);
}

void Purr::report_player(int64_t p_peer_id, const String &p_reason, const String &p_details) {
	ERR_FAIL_COND_MSG(!lobby->is_active(), "Players can be reported from a crossplay lobby they're in.");
	PurrOnline::get_singleton()->report_peer(p_peer_id, p_reason, p_details);
}

void Purr::block_player(int64_t p_peer_id) {
	ERR_FAIL_COND_MSG(!lobby->is_active(), "Players can be blocked from a crossplay lobby they're in.");
	PurrOnline::get_singleton()->block_peer(p_peer_id);
}

// Ranked play: matchmaking finds players of a similar rating and joins them in a lobby, and each
// player's game reports who finished where. The service changes ratings once enough of them
// agree (the game's ranked_agreement on the service; more than half by default).
Signal Purr::matchmake(const String &p_mode, int p_players) {
	const Ref<PurrOnlineCall> call = new_call();
	if (!online_lobbies()) {
		complete(call, failure("unavailable", "Matchmaking needs the online service, signed in."));
		return signal_of(call);
	}
	lobby->matchmake(p_mode, p_players, call);
	return signal_of(call);
}

void Purr::matchmake_cancel() {
	if (lobby->is_matchmaking()) {
		lobby->cancel_matchmaking();
	}
}

void Purr::report_result(const Array &p_ranking) {
	ERR_FAIL_COND_MSG(!lobby->is_active(), "Results are reported from the matchmade lobby they're for.");
	PurrOnline::get_singleton()->report_result(p_ranking);
}

// Checks: at moments the game picks, every player's game sends something they can all work out
// (a lap time, a score, the shared state). Only a hash of it leaves the device. Once everyone
// has, check_completed says whether they agreed and who didn't.
void Purr::check(const String &p_name, const Variant &p_value) {
	ERR_FAIL_COND_MSG(!lobby->is_active(), "Checks run in a crossplay lobby.");
	PurrOnline::get_singleton()->send_check(p_name, VariantUtilityFunctions::var_to_str(p_value).sha256_text());
}

// Flags: this player's game judged another player's behaviour impossible (moving faster than the
// game allows, say). When most of the other players' games flag the same player for the same
// reason within a few seconds, player_flagged fires on everyone's device.
void Purr::flag(int64_t p_peer_id, const String &p_reason) {
	ERR_FAIL_COND_MSG(!lobby->is_active(), "Players are flagged from a crossplay lobby.");
	PurrOnline::get_singleton()->flag_peer(p_peer_id, p_reason);
}

void Purr::online_update_required(const Variant &p_min_version, const Variant &p_max_version) {
	Account::get_singleton()->emit_signal(SNAME("update_required"), p_min_version, p_max_version);
}

void Purr::online_service_outdated() {
	Account::get_singleton()->emit_signal(SNAME("service_outdated"));
}

// Players without a platform name (a device account, or no online service) choose their own:
// the game asks when this is true.
bool Purr::needs_display_name() const {
	return player.is_valid() && (provider == "device" || provider == "local") && !name_chosen;
}

// The player's chosen name, kept on their account (whatever platform they sign in with later) and
// on this device. 1 to 24 characters; it's shown to other players.
Signal Purr::set_display_name(const String &p_name) {
	const Ref<PurrOnlineCall> call = new_call();
	const String name = p_name.strip_edges();
	if (name.is_empty() || name.length() > MAX_NAME_LENGTH) {
		complete(call, failure("bad_name", vformat("A name is 1 to %d characters.", MAX_NAME_LENGTH)));
		return signal_of(call);
	}
	if (PurrOnline::get_singleton()->is_configured() && !PurrOnline::get_singleton()->get_token().is_empty()) {
		Signal saved = PurrOnline::get_singleton()->set_display_name(name);
		saved.connect(callable_mp(this, &Purr::display_name_saved).bind(call), CONNECT_ONE_SHOT);
		return signal_of(call);
	}
	Dictionary result;
	result["ok"] = true;
	result["display_name"] = name;
	display_name_saved(result, call);
	return signal_of(call);
}

void Purr::display_name_saved(const Dictionary &p_result, const Ref<RefCounted> &p_call) {
	if (!p_result.get("ok", false)) {
		complete(p_call, p_result);
		return;
	}
	const String name = p_result.has("data") ? String(Dictionary(p_result["data"]).get("display_name", "")) : String(p_result.get("display_name", ""));
	name_chosen = true;
	PurrCredentials::store(CHOSEN_NAME_KEY, name);
	if (player.is_valid()) {
		player->set_player_name(name);
	}
	Dictionary result;
	result["ok"] = true;
	result["display_name"] = name;
	complete(p_call, result);
}

Signal Purr::get_service_version() {
	return PurrOnline::get_singleton()->get_service_version();
}

Signal Purr::get_rating(const String &p_mode) {
	return PurrOnline::get_singleton()->get_rating(p_mode);
}

void Purr::invite_friends() {
	friends->invite_with_platform_screen();
}

// Local players.

Ref<LocalPlayer> Purr::add_guest(const Ref<LocalPlayer> &p_remembered) {
	Ref<LocalPlayer> guest = p_remembered;
	if (guest.is_null()) {
		guest.instantiate();
		guest->setup(LocalPlayer::KIND_GUEST, "Guest", String(), String(), false, PlayerSaves::BACKEND_LOCAL);
	} else if (guest->get_saves().is_valid()) {
		guest->get_saves()->sync();
	}
	if (!local_players.has(guest)) {
		local_players.push_back(guest);
	}
	return guest;
}

void Purr::remove_guest(const Ref<LocalPlayer> &p_guest) {
	ERR_FAIL_COND_MSG(p_guest == player, "Player 1 can't be removed.");
	local_players.erase(p_guest);
	// A guest who brought their profile without being remembered leaves nothing behind on this
	// device, once their saves have gone up to their account.
	if (p_guest.is_valid() && p_guest->get_kind() == LocalPlayer::KIND_ONLINE_GUEST && !p_guest->is_remembered() && !p_guest->get_saves()->has_unsynced()) {
		p_guest->get_saves()->delete_all();
	}
}

// A guest bringing their profile: they type their email address here, then the code they get.

Signal Purr::guest_email_start(const String &p_email) {
	const Ref<PurrOnlineCall> call = new_call();
	pending_guest_email = p_email.strip_edges();
	pending_guest_challenge = String();
	Signal sent = PurrOnline::get_singleton()->guest_email_start(pending_guest_email);
	sent.connect(callable_mp(this, &Purr::email_code_sent).bind(call, true), CONNECT_ONE_SHOT);
	return signal_of(call);
}

// A code is on its way: its challenge stays here for the finishing call.
void Purr::email_code_sent(const Dictionary &p_result, const Ref<RefCounted> &p_call, bool p_guest) {
	const String challenge = p_result.get("ok", false) ? String(Dictionary(p_result["data"]).get("challenge", "")) : String();
	if (p_guest) {
		pending_guest_challenge = challenge;
	} else {
		pending_challenge = challenge;
	}
	complete(p_call, p_result);
}

Signal Purr::guest_email_finish(const String &p_code, bool p_remember, const String &p_name) {
	const Ref<PurrOnlineCall> call = new_call();
	if (pending_guest_challenge.is_empty()) {
		complete(call, failure("no_code_requested", "Call guest_email_start() first."));
		return signal_of(call);
	}
	Signal verified = PurrOnline::get_singleton()->guest_email_verify(pending_guest_email, pending_guest_challenge, p_code, p_remember, get_device_label(), p_name);
	verified.connect(callable_mp(this, &Purr::guest_email_verified).bind(call, p_remember), CONNECT_ONE_SHOT);
	return signal_of(call);
}

void Purr::guest_email_verified(const Dictionary &p_result, const Ref<RefCounted> &p_call, bool p_remember) {
	if (!p_result.get("ok", false)) {
		complete(p_call, p_result);
		return;
	}
	pending_guest_email = String();
	pending_guest_challenge = String();
	const Dictionary data = p_result["data"];
	Ref<LocalPlayer> guest;
	guest.instantiate();
	const bool remembered = data.get("kind", "") == "grant";
	guest->setup(LocalPlayer::KIND_ONLINE_GUEST, data.get("display_name", "Guest"), data["account_id"], data["token"], remembered, PlayerSaves::BACKEND_ONLINE);
	if (remembered) {
		store_remembered(guest);
	}
	add_guest(guest);
	LocalPlayers::get_singleton()->emit_signal(SNAME("guest_added"), guest);
	Dictionary result;
	result["ok"] = true;
	result["guest"] = guest;
	complete(p_call, result);
}

// Player 1 adding an email: from then on the online service keeps their saves, and every
// platform signed in with the same address shares them. With an email already, this changes it.

Signal Purr::email_link_start(const String &p_email, const String &p_current_email) {
	const Ref<PurrOnlineCall> call = new_call();
	if (!is_available("linking")) {
		complete(call, failure("unavailable", "Adding an email needs the online service, signed in."));
		return signal_of(call);
	}
	pending_email = p_email.strip_edges();
	pending_current_email = p_current_email.strip_edges();
	pending_challenge = String();
	Signal sent = PurrOnline::get_singleton()->email_start(pending_email);
	sent.connect(callable_mp(this, &Purr::email_code_sent).bind(call, false), CONNECT_ONE_SHOT);
	return signal_of(call);
}

Signal Purr::email_link_finish(const String &p_code) {
	const Ref<PurrOnlineCall> call = new_call();
	if (pending_challenge.is_empty()) {
		complete(call, failure("no_code_requested", "Call Account.link_email() first."));
		return signal_of(call);
	}
	Signal verified = PurrOnline::get_singleton()->email_verify(pending_email, pending_challenge, p_code, email_added ? pending_current_email : String());
	verified.connect(callable_mp(this, &Purr::email_verified).bind(call), CONNECT_ONE_SHOT);
	return signal_of(call);
}

void Purr::email_verified(const Dictionary &p_result, const Ref<RefCounted> &p_call) {
	if (!p_result.get("ok", false)) {
		complete(p_call, p_result);
		return;
	}
	pending_email = String();
	pending_challenge = String();
	pending_current_email = String();
	const Dictionary data = p_result["data"];
	PurrCredentials::store(LAST_ACCOUNT_KEY, data["account_id"]);
	PurrCredentials::store(LAST_ACCOUNT_EMAIL_KEY, "1");
	const bool was_added = email_added;
	email_added = true;
	if (!was_added && player.is_valid()) {
		// This device's saves go up to the account (now on the service); where the account
		// already has a save in a slot, save_conflict lets the player choose.
		const Ref<PlayerSaves> before = player->get_saves();
		player->setup(LocalPlayer::KIND_PLAYER, player->get_player_name(), data["account_id"], String(), false, save_backend(true));
		attach_saves();
		player->get_saves()->adopt(before);
	}
	Dictionary result;
	result["ok"] = true;
	result["merged"] = data.get("merged", false);
	complete(p_call, result);
}

// The devices that remember the player (as guests on others' devices), and the account itself.

Signal Purr::get_devices() {
	const Ref<PurrOnlineCall> call = new_call();
	if (!online_lobbies()) {
		complete(call, failure("offline", "Devices need the online service, signed in."));
		return signal_of(call);
	}
	PurrOnline::get_singleton()->list_grants().connect(callable_mp_static(+[](const Dictionary &p_result, const Ref<RefCounted> &p_call) {
		Dictionary result;
		result["ok"] = p_result.get("ok", false);
		if (bool(result["ok"])) {
			// Each device: its id (for forget_device), label, and when it was added and last used.
			result["devices"] = Dictionary(p_result.get("data", Dictionary())).get("grants", Array());
		} else {
			result["error"] = p_result.get("error", "");
			result["message"] = p_result.get("message", "");
		}
		emit_completed(p_call, result);
	}).bind(call), CONNECT_ONE_SHOT);
	return signal_of(call);
}

Signal Purr::forget_device(const String &p_device_id) {
	if (!online_lobbies()) {
		return deferred_result(failure("offline", "Devices need the online service, signed in."));
	}
	return PurrOnline::get_singleton()->revoke_grant(p_device_id);
}

Signal Purr::delete_account() {
	const Ref<PurrOnlineCall> call = new_call();
	if (!online_lobbies()) {
		complete(call, failure("offline", "Deleting the account needs the online service, signed in."));
		return signal_of(call);
	}
	PurrOnline::get_singleton()->delete_account().connect(callable_mp(this, &Purr::account_deleted).bind(call), CONNECT_ONE_SHOT);
	return signal_of(call);
}

// The account is gone from the service: this device forgets it too, and the player is signed out
// (signing in again starts a new account). Saves on this device stay.
void Purr::account_deleted(const Dictionary &p_result, const Ref<RefCounted> &p_call) {
	if (!p_result.get("ok", false)) {
		emit_completed(p_call, p_result);
		return;
	}
	lobby_leave();
	PurrOnline::get_singleton()->sign_out();
	for (const char *key : { LAST_ACCOUNT_KEY, LAST_ACCOUNT_EMAIL_KEY, CHOSEN_NAME_KEY, DEVICE_SECRET_KEY }) {
		PurrCredentials::erase(key);
	}
	email_added = false;
	name_chosen = false;
	player.unref();
	local_players.clear();
	Dictionary result;
	result["ok"] = true;
	emit_completed(p_call, result);
}

// Remembered guests and local profiles, kept in the system keychain.

void Purr::load_remembered() {
	remembered_guests.clear();
	const String text = PurrCredentials::load(REMEMBERED_KEY, true);
	const Variant stored = text.is_empty() ? Variant() : JSON::parse_string(text);
	if (stored.get_type() != Variant::ARRAY) {
		return;
	}
	for (const Variant &entry : Array(stored)) {
		const Dictionary each = entry;
		Ref<LocalPlayer> guest;
		guest.instantiate();
		if (each.get("kind", "") == "local") {
			guest->setup_local_profile(each.get("name", "Guest"), each.get("profile_id", ""), local_profile_backend());
		} else {
			guest->setup(LocalPlayer::KIND_ONLINE_GUEST, each.get("name", "Guest"), each.get("account_id", ""), each.get("token", ""), true, PlayerSaves::BACKEND_ONLINE);
		}
		remembered_guests.push_back(guest);
	}
}

static void write_remembered(const Array &p_guests) {
	Array stored;
	for (const Variant &entry : p_guests) {
		const Ref<LocalPlayer> guest = entry;
		Dictionary each;
		each["name"] = guest->get_player_name();
		if (guest->get_kind() == LocalPlayer::KIND_LOCAL_PROFILE) {
			each["kind"] = "local";
			each["profile_id"] = guest->get_profile_id();
		} else {
			each["kind"] = "online";
			each["account_id"] = guest->get_account_id();
			each["token"] = guest->get_token();
		}
		stored.push_back(each);
	}
	PurrCredentials::store(REMEMBERED_KEY, JSON::stringify(stored), true);
}

void Purr::store_remembered(const Ref<LocalPlayer> &p_guest) {
	if (!remembered_guests.has(p_guest)) {
		remembered_guests.push_back(p_guest);
	}
	write_remembered(remembered_guests);
}

// Local profiles keep their saves like player 1 would without the online service: in the
// platform's cloud, or on this device.
PlayerSaves::Backend Purr::local_profile_backend() const {
	return save_backend(false);
}

void Purr::forget_remembered(const Ref<LocalPlayer> &p_guest) {
	remembered_guests.erase(p_guest);
	local_players.erase(p_guest);
	write_remembered(remembered_guests);
}

void Purr::_bind_methods() {
}
