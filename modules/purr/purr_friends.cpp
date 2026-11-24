#include "purr_friends.h"

#include "friends.h"
#include "purr.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"

namespace {

// Purr's lobby codes (the online service's alphabet) and GameKit's party code alphabet
// (GKGameActivity.validPartyCodeAlphabet).
const char *CODE_ALPHABET = "23456789ABCDEFGHJKMNPQRSTVWXYZ";
const char *PARTY_ALPHABET = "23456789CFGHJMPQRVWX";
// Rich presence values are 256 bytes at most: room for this many Steam IDs and their commas.
const int MAX_INVITED = 14;
// Rich presence keys. "connect" is what Steam's own "Join Game" and invites start the game with.
const char *LOBBY_KEY = "purr_lobby";
const char *INVITED_KEY = "purr_invited";

Object *steam() {
	return Purr::get_singleton()->steam();
}

uint64_t steam_id(const String &p_id) {
	return p_id.to_int();
}

String steam_id_string(const Variant &p_id) {
	return String::num_uint64((uint64_t)(int64_t)p_id);
}

} // namespace

// Bijective base 30: every code is one number and back, and no code is 0.
int64_t PurrFriends::code_to_number(const String &p_code) {
	const String alphabet = CODE_ALPHABET;
	const String code = p_code.to_upper();
	if (code.is_empty() || code.length() > 12) {
		return 0;
	}
	int64_t number = 0;
	for (int i = 0; i < code.length(); i++) {
		const int digit = alphabet.find_char(code[i]);
		if (digit < 0) {
			return 0;
		}
		number = number * alphabet.length() + digit + 1;
	}
	return number;
}

String PurrFriends::number_to_code(int64_t p_number) {
	const String alphabet = CODE_ALPHABET;
	String code;
	while (p_number > 0) {
		const int64_t digit = (p_number - 1) % alphabet.length();
		code = String::chr(alphabet[digit]) + code;
		p_number = (p_number - 1) / alphabet.length();
	}
	return code;
}

// The code's number in base 20, padded to two halves of 4 to 6 characters.
String PurrFriends::code_to_party_code(const String &p_code) {
	const String alphabet = PARTY_ALPHABET;
	int64_t number = code_to_number(p_code);
	if (number == 0) {
		return String();
	}
	String digits;
	while (number > 0) {
		digits = String::chr(alphabet[number % alphabet.length()]) + digits;
		number /= alphabet.length();
	}
	const int half = MAX(4, (digits.length() + 1) / 2);
	if (half > 6) {
		return String();
	}
	digits = digits.lpad(half * 2, String::chr(alphabet[0]));
	return digits.substr(0, half) + "-" + digits.substr(half);
}

String PurrFriends::party_code_to_code(const String &p_party_code) {
	const String alphabet = PARTY_ALPHABET;
	const String digits = p_party_code.to_upper().replace("-", "");
	if (digits.is_empty() || digits.length() > 12) {
		return String();
	}
	int64_t number = 0;
	for (int i = 0; i < digits.length(); i++) {
		const int digit = alphabet.find_char(digits[i]);
		if (digit < 0) {
			return String();
		}
		number = number * alphabet.length() + digit;
	}
	return number_to_code(number);
}

void PurrFriends::start() {
	if (started) {
		return;
	}
	started = true;
	Purr *purr = Purr::get_singleton();
	if (purr->steam()) {
		steam()->connect(SNAME("persona_state_change"), callable_mp(this, &PurrFriends::steam_friends_changed));
		steam()->connect(SNAME("friend_rich_presence_update"), callable_mp(this, &PurrFriends::steam_friends_changed));
		steam()->connect(SNAME("lobby_invite"), callable_mp(this, &PurrFriends::steam_lobby_invited));
	}
	if (purr->game_center()) {
		purr->game_center()->connect(SNAME("invite_accepted"), callable_mp(this, &PurrFriends::game_center_invite_accepted));
		purr->game_center()->connect(SNAME("party_joined"), callable_mp(this, &PurrFriends::game_center_party_joined));
	}
}

// A friend's name, status or rich presence changed.
void PurrFriends::steam_friends_changed(int64_t p_steam_id, int64_t p_detail) {
	changed();
}

// An invite to a Steam lobby from Steam's own invite screen (Purr's own invites are in the
// inviter's rich presence). Accepting it in Steam joins at once; this is for the game's list.
void PurrFriends::steam_lobby_invited(int64_t p_inviter, int64_t p_lobby, int64_t p_game) {
	lobby_invites[steam_id_string(p_inviter)] = steam_id_string(p_lobby);
	changed();
}

void PurrFriends::game_center_invite_accepted(int64_t p_player_group) {
	const String code = number_to_code(p_player_group);
	if (!code.is_empty()) {
		Purr::get_singleton()->join_invited(code);
	}
}

void PurrFriends::game_center_party_joined(const String &p_party_code) {
	const String code = party_code_to_code(p_party_code);
	if (!code.is_empty() && code != lobby_code) {
		Purr::get_singleton()->join_invited(code);
	}
}

// Steam sends a burst of these: the game hears once a frame.
void PurrFriends::changed() {
	if (!changed_queued) {
		changed_queued = true;
		callable_mp(this, &PurrFriends::emit_changed).call_deferred();
	}
}

void PurrFriends::emit_changed() {
	changed_queued = false;
	Friends::get_singleton()->emit_signal(SNAME("changed"));
}

void PurrFriends::set_lobby(const String &p_code, bool p_crossplay) {
	if (p_code == lobby_code) {
		return;
	}
	lobby_code = p_code;
	crossplay = p_crossplay;
	invited.clear();
	write_presence();

	Purr *purr = Purr::get_singleton();
	const String activity = GLOBAL_GET("game_center/party_activity");
	if (activity.is_empty() || !purr->game_center()) {
		return;
	}
	const String party_code = crossplay ? code_to_party_code(lobby_code) : String();
	if (!party_code.is_empty() && purr->game_center_ready()) {
		purr->game_center()->call("start_party", activity, party_code);
	} else {
		purr->game_center()->call("end_party");
	}
}

// Setting a key to empty removes it; the game's own keys stay.
void PurrFriends::write_presence() {
	if (!Purr::get_singleton()->steam_ready()) {
		return;
	}
	const String connect = lobby_code.is_empty() ? String() : vformat("%s %s", crossplay ? PURR_INVITE_ARGUMENT : STEAM_LOBBY_ARGUMENT, lobby_code);
	steam()->call("setRichPresence", LOBBY_KEY, lobby_code);
	steam()->call("setRichPresence", "connect", connect);
	steam()->call("setRichPresence", "steam_player_group", lobby_code);
	steam()->call("setRichPresence", INVITED_KEY, String(",").join(invited));
}

bool PurrFriends::is_available() const {
	Purr *purr = Purr::get_singleton();
	return purr->steam_ready() || purr->game_center_ready() || purr->play_games_ready();
}

void PurrFriends::ask_platform_friends() {
	Purr *purr = Purr::get_singleton();
	Object *platform = purr->game_center_ready() ? purr->game_center() : purr->play_games_ready() ? purr->play_games() : nullptr;
	if (!platform || platform_friends_asked) {
		return;
	}
	platform_friends_asked = true;
	Signal loaded = platform->call("load_friends");
	loaded.connect(callable_mp(this, &PurrFriends::platform_friends_loaded), CONNECT_ONE_SHOT);
}

// Neither platform says who's playing or where: those stay false and empty.
void PurrFriends::platform_friends_loaded(const Dictionary &p_result) {
	if (!p_result.get("ok", false)) {
		return;
	}
	platform_friends.clear();
	service_ids.clear();
	// Game Center signs players in to the online services by their teamPlayerID; Play Games by
	// the ID it gives.
	const String provider = Purr::get_singleton()->game_center_ready() ? "game_center" : "play_games";
	for (const Variant &entry : Array(p_result.get("friends", Array()))) {
		const Dictionary loaded = entry;
		Dictionary service_id;
		service_id["provider"] = provider;
		service_id["id"] = provider == "game_center" ? loaded.get("team_player_id", "") : loaded.get("id", "");
		if (!String(service_id["id"]).is_empty()) {
			service_ids.push_back(service_id);
		}
		Dictionary friend_entry;
		friend_entry["id"] = loaded.get("id", "");
		friend_entry["name"] = loaded.get("name", "");
		friend_entry["playing"] = false;
		friend_entry["lobby"] = String();
		friend_entry["invited"] = false;
		platform_friends.push_back(friend_entry);
	}
	changed();
}

Array PurrFriends::get_service_ids() {
	if (!Purr::get_singleton()->steam_ready()) {
		ask_platform_friends();
		return service_ids.duplicate(true);
	}
	Array ids;
	for (const Variant &entry : get_friends()) {
		Dictionary service_id;
		service_id["provider"] = "steam";
		service_id["id"] = Dictionary(entry).get("id", "");
		ids.push_back(service_id);
	}
	return ids;
}

Array PurrFriends::get_friends() {
	Array friends;
	Purr *purr = Purr::get_singleton();
	if (!purr->steam_ready()) {
		ask_platform_friends();
		return platform_friends.duplicate(true);
	}
	const int64_t immediate = purr->steam_constant("FRIEND_FLAG_IMMEDIATE");
	const int64_t app_id = steam()->call("getAppID");
	const String own_id = steam_id_string(steam()->call("getSteamID"));
	const int count = steam()->call("getFriendCount", immediate);
	for (int i = 0; i < count; i++) {
		const Variant id = steam()->call("getFriendByIndex", i, immediate);
		const String id_string = steam_id_string(id);
		const Dictionary game = steam()->call("getFriendGamePlayed", id);
		const bool playing = !game.is_empty() && (int64_t)game.get("id", 0) == app_id;
		String code;
		bool invited_here = false;
		if (playing) {
			code = steam()->call("getFriendRichPresence", id, LOBBY_KEY);
			const PackedStringArray invitees = String(steam()->call("getFriendRichPresence", id, INVITED_KEY)).split(",", false);
			const HashMap<String, String>::ConstIterator lobby_invite = lobby_invites.find(id_string);
			invited_here = !code.is_empty() && (invitees.has(own_id) || (lobby_invite && lobby_invite->value == code));
		}
		Dictionary entry;
		entry["id"] = id_string;
		entry["name"] = steam()->call("getFriendPersonaName", id);
		entry["playing"] = playing;
		entry["lobby"] = code;
		entry["invited"] = invited_here;
		friends.push_back(entry);
	}
	return friends;
}

void PurrFriends::invite(const String &p_friend_id) {
	ERR_FAIL_COND_MSG(lobby_code.is_empty(), "Create or join a lobby before inviting friends.");
	Purr *purr = Purr::get_singleton();
	if (!purr->steam_ready() && crossplay && purr->game_center_ready()) {
		purr->game_center()->call("invite_friend", p_friend_id, code_to_number(lobby_code), (int64_t)GLOBAL_GET("lobbies/max_players"));
		return;
	}
	ERR_FAIL_COND_MSG(!purr->steam_ready(), "Inviting a friend needs Steam or Game Center; Lobbies.invite_friends() opens the platform's invite screen.");
	if (crossplay) {
		steam()->call("inviteUserToGame", steam_id(p_friend_id), vformat("%s %s", PURR_INVITE_ARGUMENT, lobby_code));
	} else {
		steam()->call("inviteUserToLobby", steam_id(lobby_code), steam_id(p_friend_id));
	}
	const int64_t at = invited.find(p_friend_id);
	if (at >= 0) {
		invited.remove_at(at);
	}
	invited.push_back(p_friend_id);
	if (invited.size() > MAX_INVITED) {
		invited.remove_at(0);
	}
	write_presence();
}

bool PurrFriends::can_invite() const {
	Purr *purr = Purr::get_singleton();
	return purr->steam_ready() || (purr->game_center_ready() && (purr->online_lobbies() || PurrGameCenterLobby::is_supported()));
}

void PurrFriends::invite_with_platform_screen() {
	ERR_FAIL_COND_MSG(lobby_code.is_empty(), "Create or join a lobby before inviting friends.");
	Purr *purr = Purr::get_singleton();
	if (purr->steam_ready()) {
		if (crossplay) {
			steam()->call("activateGameOverlayInviteDialogConnectString", vformat("%s %s", PURR_INVITE_ARGUMENT, lobby_code));
		} else {
			steam()->call("activateGameOverlayInviteDialog", steam_id(lobby_code));
		}
		return;
	}
	ERR_FAIL_COND_MSG(!crossplay || !purr->game_center_ready(), "Invites need Steam or Game Center; share the lobby's code instead.");
	purr->game_center()->call("invite_players", code_to_number(lobby_code), (int64_t)GLOBAL_GET("lobbies/max_players"));
}
