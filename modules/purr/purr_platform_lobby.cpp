#include "purr_platform_lobby.h"

#include "lobbies.h"
#include "purr.h"

#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/variant/variant_utility.h"

namespace {

Ref<PlayerSaves> saves() {
	const Ref<LocalPlayer> player = Purr::get_singleton() ? Purr::get_singleton()->get_player() : Ref<LocalPlayer>();
	return player.is_valid() ? player->get_saves() : Ref<PlayerSaves>();
}

Dictionary multiplayer_data() {
	const Ref<PlayerSaves> player_saves = saves();
	if (player_saves.is_null() || !player_saves->has(PURR_MULTIPLAYER_SLOT)) {
		return Dictionary();
	}
	const Variant data = player_saves->load(PURR_MULTIPLAYER_SLOT);
	return data.get_type() == Variant::DICTIONARY ? Dictionary(data) : Dictionary();
}

} // namespace

PurrPlatformLobby::PurrPlatformLobby() {
	referee.instantiate();
	referee->connect(SNAME("check_completed"), callable_mp(this, &PurrPlatformLobby::referee_check_completed));
	referee->connect(SNAME("player_flagged"), callable_mp(this, &PurrPlatformLobby::referee_player_flagged));
	referee->connect(SNAME("result_agreed"), callable_mp(this, &PurrPlatformLobby::referee_result_agreed));
	referee->connect(SNAME("member_kicked"), callable_mp(this, &PurrPlatformLobby::referee_member_kicked));
}

bool PurrPlatformLobby::is_host() const {
	return active && !referee->get_host().is_empty() && referee->get_host() == _own_member();
}

void PurrPlatformLobby::entered(const String &p_code) {
	code = p_code;
	active = true;
	const Ref<RefCounted> call = pending_call;
	pending_call.unref();
	Dictionary result;
	result["ok"] = true;
	result["code"] = p_code;
	if (call.is_valid()) {
		purr_complete(call, result);
	}
	Lobbies::get_singleton()->call_deferred(SNAME("emit_signal"), SNAME("joined"), p_code);
}

void PurrPlatformLobby::failed(const String &p_error, const String &p_message) {
	const Ref<RefCounted> call = pending_call;
	leave();
	if (call.is_valid()) {
		purr_complete(call, purr_failure(p_error, p_message));
	}
}

void PurrPlatformLobby::member_joined(const String &p_member, int64_t p_peer_id) {
	referee->add_member(p_member, p_peer_id);
	// The host turns away players it removed (they don't know, having joined since) and players it
	// blocked; everyone else's game drops them too, on the host's word.
	if (is_host() && p_member != _own_member() && (referee->is_kicked(p_member) || turned_away(p_member))) {
		_send(PurrLobbyReferee::make_kick(p_member));
	}
}

void PurrPlatformLobby::member_left(const String &p_member) {
	referee->remove_member(p_member);
}

void PurrPlatformLobby::received(const String &p_from, const Dictionary &p_message) {
	referee->receive(p_from, p_message, OS::get_singleton()->get_ticks_msec());
}

void PurrPlatformLobby::host_is(const String &p_member, bool p_changed) {
	referee->set_host(p_member);
	if (p_changed) {
		Lobbies::get_singleton()->call_deferred(SNAME("emit_signal"), SNAME("host_changed"), p_member == _own_member());
	}
}

void PurrPlatformLobby::start_ranked(const String &p_mode) {
	mode = p_mode;
	referee->set_ranked(true);
	_send(PurrLobbyReferee::make_rating(own_rating(p_mode)));
}

bool PurrPlatformLobby::turned_away(const String &p_member) const {
	return is_blocked(_platform(), p_member);
}

// Also stops whatever the platform was still doing (creating, joining, matchmaking).
void PurrPlatformLobby::leave() {
	active = false;
	code = String();
	mode = String();
	pending_call.unref();
	// Nothing carries over to the next lobby.
	referee->reset();
	_close();
}

// The host removes a player: every game drops them, and theirs leaves (emitting kicked). The host
// keeps turning them away for as long as the lobby lasts.
void PurrPlatformLobby::kick(int64_t p_peer_id) {
	ERR_FAIL_COND_MSG(!is_host(), "Only the host of a lobby can remove players.");
	const String member = referee->get_member(p_peer_id);
	ERR_FAIL_COND_MSG(member.is_empty() || member == _own_member(), vformat("There's no other player with peer ID %d in the lobby.", p_peer_id));
	_send(PurrLobbyReferee::make_kick(member));
}

void PurrPlatformLobby::check(const String &p_name, const Variant &p_value) {
	ERR_FAIL_COND_MSG(!active, "Checks run in a lobby.");
	ERR_FAIL_COND_MSG(!PurrLobbyReferee::is_valid_check_name(p_name), "A check's name is 1 to 64 of A-Z, a-z, 0-9, _ . : and -.");
	_send(PurrLobbyReferee::make_check(p_name, VariantUtilityFunctions::var_to_str(p_value).sha256_text()));
}

void PurrPlatformLobby::flag(int64_t p_peer_id, const String &p_reason) {
	ERR_FAIL_COND_MSG(!active, "Players are flagged from a lobby they're in.");
	ERR_FAIL_COND_MSG(!PurrLobbyReferee::is_valid_flag_reason(p_reason), "A flag's reason is 1 to 32 of A-Z, a-z, 0-9, _ . : and -.");
	const String member = referee->get_member(p_peer_id);
	ERR_FAIL_COND_MSG(member.is_empty(), vformat("There's no player with peer ID %d in the lobby.", p_peer_id));
	_send(PurrLobbyReferee::make_flag(member, p_reason));
}

void PurrPlatformLobby::report_result(const Array &p_ranking) {
	ERR_FAIL_COND_MSG(!active || mode.is_empty(), "Results are reported from the matchmade lobby they're for.");
	PackedStringArray ranking;
	for (const Variant &peer_id : p_ranking) {
		const String member = referee->get_member(peer_id);
		ERR_FAIL_COND_MSG(member.is_empty(), vformat("There's no player with peer ID %s in the lobby.", String(peer_id)));
		ranking.push_back(member);
	}
	_send(PurrLobbyReferee::make_result(ranking));
}

// Kept in the player's saves: never matched with them again, and turned away from lobbies this
// player hosts (removed at once if they're in this one).
void PurrPlatformLobby::block(int64_t p_peer_id) {
	ERR_FAIL_COND_MSG(!active, "Players are blocked from a lobby they're in.");
	const String member = referee->get_member(p_peer_id);
	ERR_FAIL_COND_MSG(member.is_empty() || member == _own_member(), vformat("There's no other player with peer ID %d in the lobby.", p_peer_id));
	const Ref<PlayerSaves> player_saves = saves();
	ERR_FAIL_COND(player_saves.is_null());
	Dictionary data = multiplayer_data().duplicate(true);
	Dictionary blocked = data.get("blocked", Dictionary());
	blocked[_platform() + ":" + member] = true;
	data["blocked"] = blocked;
	player_saves->save(PURR_MULTIPLAYER_SLOT, data, Dictionary());
	if (is_host()) {
		_send(PurrLobbyReferee::make_kick(member));
	}
}

void PurrPlatformLobby::report(int64_t p_peer_id) {
	const String member = referee->get_member(p_peer_id);
	ERR_FAIL_COND_MSG(member.is_empty(), vformat("There's no player with peer ID %d in the lobby.", p_peer_id));
	_report(member);
}

Dictionary PurrPlatformLobby::get_rating(const String &p_mode) {
	const Dictionary entry = Dictionary(multiplayer_data().get("ratings", Dictionary())).get(p_mode, Dictionary());
	Dictionary data;
	data["rating"] = (double)entry.get("rating", PurrLobbyReferee::START_RATING);
	data["matches"] = (int64_t)entry.get("matches", 0);
	Dictionary result;
	result["ok"] = true;
	result["data"] = data;
	return result;
}

double PurrPlatformLobby::own_rating(const String &p_mode) {
	return Dictionary(get_rating(p_mode)["data"])["rating"];
}

bool PurrPlatformLobby::is_blocked(const String &p_platform, const String &p_member) {
	return Dictionary(multiplayer_data().get("blocked", Dictionary())).has(p_platform + ":" + p_member);
}

void PurrPlatformLobby::referee_check_completed(const String &p_name, bool p_agreed, const Array &p_disagreeing) {
	Lobbies::get_singleton()->call_deferred(SNAME("emit_signal"), SNAME("check_completed"), p_name, p_agreed, p_disagreeing);
}

void PurrPlatformLobby::referee_player_flagged(int64_t p_peer_id, const String &p_reason) {
	Lobbies::get_singleton()->call_deferred(SNAME("emit_signal"), SNAME("player_flagged"), p_peer_id, p_reason);
}

// Enough players agreed on who finished where: this player's game changes its own rating.
void PurrPlatformLobby::referee_result_agreed(const Array &p_ranking, const PackedFloat64Array &p_changes) {
	const int64_t index = p_ranking.find(_own_member());
	const Ref<PlayerSaves> player_saves = saves();
	if (index < 0 || mode.is_empty() || player_saves.is_null()) {
		return;
	}
	Dictionary data = multiplayer_data().duplicate(true);
	Dictionary ratings = data.get("ratings", Dictionary());
	const Dictionary current = Dictionary(get_rating(mode)["data"]);
	Dictionary entry;
	entry["rating"] = (double)current["rating"] + p_changes[index];
	entry["matches"] = (int64_t)current["matches"] + 1;
	ratings[mode] = entry;
	data["ratings"] = ratings;
	player_saves->save(PURR_MULTIPLAYER_SLOT, data, Dictionary());
	Lobbies::get_singleton()->call_deferred(SNAME("emit_signal"), SNAME("result_recorded"));
}

void PurrPlatformLobby::referee_member_kicked(const String &p_member) {
	if (p_member == _own_member()) {
		leave();
		Lobbies::get_singleton()->call_deferred(SNAME("emit_signal"), SNAME("kicked"));
		return;
	}
	_drop(p_member);
}
