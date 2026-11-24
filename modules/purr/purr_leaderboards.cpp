#include "purr_leaderboards.h"

#include "purr.h"

#include "modules/purr_online/purr_online.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"

namespace {

Object *steam() {
	return Purr::get_singleton()->steam();
}

Dictionary failure(const String &p_error) {
	Dictionary result;
	result["ok"] = false;
	result["error"] = p_error;
	result["scores"] = Array();
	return result;
}

// A platform's answer, passed on to the waiting call.
void pass_on(const Dictionary &p_result, const Ref<RefCounted> &p_call) {
	purr_complete(p_call, p_result);
}

} // namespace

String PurrLeaderboards::platform() const {
	Purr *purr = Purr::get_singleton();
	return purr->game_center_ready() ? "game_center" : purr->play_games_ready() ? "play_games" : purr->steam_ready() ? "steam" : "";
}

String PurrLeaderboards::platform_name(const String &p_id, const String &p_platform) const {
	// Empty (as the Leaderboards tab in Project Settings leaves it): the same ID.
	const String name = ProjectSettings::get_singleton()->get_setting("leaderboards/" + p_id + "/" + p_platform, String());
	return name.is_empty() ? p_id : name;
}

// How the online services rank the board, when it resets, and the scores it takes: what its
// first score tells them.
Dictionary PurrLeaderboards::board_settings(const String &p_id) const {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	const String key = "leaderboards/" + p_id + "/";
	Dictionary board;
	board["order"] = String(settings->get_setting(key + "order", "higher")) == "lower" ? "lower" : "higher";
	const String reset = settings->get_setting(key + "reset", "never");
	board["reset"] = reset.is_empty() ? "never" : reset;
	for (const String limit : { "min", "max" }) {
		const String value = String(settings->get_setting(key + limit, String())).strip_edges();
		if (value.is_valid_int()) {
			board[limit] = value.to_int();
		}
	}
	return board;
}

void PurrLeaderboards::online_submitted(const Dictionary &p_result, const String &p_id) {
	if (!p_result.get("ok", false)) {
		WARN_PRINT(vformat("The online services didn't take the score for leaderboard \"%s\": %s", p_id, p_result.get("message", p_result.get("error", ""))));
	}
}

void PurrLeaderboards::online_scores(const Dictionary &p_result, const Ref<RefCounted> &p_call) {
	if (!p_result.get("ok", false)) {
		purr_complete(p_call, failure(p_result.get("message", p_result.get("error", ""))));
		return;
	}
	const Dictionary data = p_result.get("data", Dictionary());
	Dictionary result;
	result["ok"] = true;
	result["scores"] = data.get("scores", Array());
	purr_complete(p_call, result);
}

void PurrLeaderboards::submit(const String &p_id, int64_t p_score) {
	if (Purr::get_singleton()->online_lobbies()) {
		Signal sent = PurrOnline::get_singleton()->submit_score(p_id, p_score, board_settings(p_id));
		sent.connect(callable_mp_static(&PurrLeaderboards::online_submitted).bind(p_id), CONNECT_ONE_SHOT);
	}
	// The platform's own board too, so its screens (show()) have the score.
	const String where = platform();
	const String name = platform_name(p_id, where);
	if (where == "game_center") {
		Purr::get_singleton()->game_center()->call("submit_score", name, p_score);
	} else if (where == "play_games") {
		Purr::get_singleton()->play_games()->call("submit_score", name, p_score);
	} else if (where == "steam") {
		with_steam_leaderboard(name, callable_mp(this, &PurrLeaderboards::steam_upload).bind(p_score));
	}
}

// "global" (the best scores), "friends" (the best among the player's friends) or
// "around_player" (those around the player's own).
void PurrLeaderboards::load_scores(const String &p_id, const String &p_scope, int p_count, bool p_previous, const Ref<RefCounted> &p_call) {
	if (p_scope != "global" && p_scope != "friends" && p_scope != "around_player") {
		purr_complete(p_call, failure("The scope is \"global\", \"friends\" or \"around_player\"."));
		return;
	}
	if (Purr::get_singleton()->online_lobbies()) {
		const Array friends = p_scope == "friends" ? Purr::get_singleton()->get_friend_service_ids() : Array();
		Signal loaded = PurrOnline::get_singleton()->query_scores(p_id, p_scope, CLAMP(p_count, 1, 100), p_previous, friends);
		loaded.connect(callable_mp_static(&PurrLeaderboards::online_scores).bind(p_call), CONNECT_ONE_SHOT);
		return;
	}
	if (p_previous) {
		purr_complete(p_call, failure("Earlier periods are only on the online services' leaderboards."));
		return;
	}
	const String where = platform();
	const String name = platform_name(p_id, where);
	const int count = CLAMP(p_count, 1, 100);
	if (where == "steam") {
		with_steam_leaderboard(name, callable_mp(this, &PurrLeaderboards::steam_download).bind(p_scope, count, p_call));
		return;
	}
	if (where.is_empty()) {
		purr_complete(p_call, failure("Leaderboards need Steam, Game Center or Play Games."));
		return;
	}
	Object *platform_object = where == "game_center" ? Purr::get_singleton()->game_center() : Purr::get_singleton()->play_games();
	const int scope = p_scope == "friends" ? 1 : p_scope == "around_player" ? 2 : 0; // the platforms' Scope
	Signal loaded = platform_object->call("get_scores", name, scope, count);
	loaded.connect(callable_mp_static(&pass_on).bind(p_call), CONNECT_ONE_SHOT);
}

void PurrLeaderboards::show(const String &p_id) {
	const String where = platform();
	const String name = p_id.is_empty() ? String() : platform_name(p_id, where);
	if (where == "game_center") {
		Purr::get_singleton()->game_center()->call("show_leaderboard", name);
	} else if (where == "play_games") {
		Purr::get_singleton()->play_games()->call("show_leaderboard", name);
	} else if (where == "steam") {
		if (name.is_empty()) {
			steam_show(0);
		} else {
			with_steam_leaderboard(name, callable_mp(this, &PurrLeaderboards::steam_show));
		}
	} else {
		ERR_PRINT("Leaderboards have no screen without Steam, Game Center or Play Games.");
	}
}

// Steam.

void PurrLeaderboards::with_steam_leaderboard(const String &p_name, const Callable &p_then) {
	if (steam_handles.has(p_name)) {
		p_then.call(steam_handles[p_name]);
		return;
	}
	steam_waiting[p_name].push_back(p_then);
	if (!steam_to_find.has(p_name)) {
		steam_to_find.push_back(p_name);
		if (steam_to_find.size() == 1) {
			find_next_steam_leaderboard();
		}
	}
}

void PurrLeaderboards::find_next_steam_leaderboard() {
	if (steam_to_find.is_empty()) {
		return;
	}
	const Callable found = callable_mp(this, &PurrLeaderboards::steam_leaderboard_found);
	if (!steam()->is_connected(SNAME("leaderboard_find_result"), found)) {
		steam()->connect(SNAME("leaderboard_find_result"), found);
		steam()->connect(SNAME("leaderboard_scores_downloaded"), callable_mp(this, &PurrLeaderboards::steam_scores_downloaded));
	}
	steam()->call("findLeaderboard", steam_to_find[0]);
}

void PurrLeaderboards::steam_leaderboard_found(int64_t p_handle, int64_t p_found) {
	ERR_FAIL_COND(steam_to_find.is_empty());
	const String name = steam_to_find[0];
	steam_to_find.remove_at(0);
	const Vector<Callable> waiting = steam_waiting[name];
	steam_waiting.erase(name);
	if (p_found) {
		steam_handles[name] = p_handle;
		for (const Callable &then : waiting) {
			then.call((uint64_t)p_handle);
		}
	} else {
		// Whatever waits for it hears it isn't there (handle 0).
		WARN_PRINT("Steam has no leaderboard named \"" + name + "\".");
		for (const Callable &then : waiting) {
			then.call((uint64_t)0);
		}
	}
	find_next_steam_leaderboard();
}

void PurrLeaderboards::steam_upload(uint64_t p_handle, int64_t p_score) {
	if (p_handle == 0) {
		return;
	}
	// Kept only if it beats the player's best.
	steam()->call("uploadLeaderboardScore", p_score, true, PackedInt32Array(), p_handle);
}

void PurrLeaderboards::steam_download(uint64_t p_handle, const String &p_scope, int p_count, const Ref<RefCounted> &p_call) {
	if (p_handle == 0) {
		purr_complete(p_call, failure("Steam has no leaderboard by that name."));
		return;
	}
	Purr *purr = Purr::get_singleton();
	steam_downloads[p_handle].push_back({ p_call, p_scope, p_count });
	if (p_scope == "friends") {
		steam()->call("downloadLeaderboardEntries", 0, 0, purr->steam_constant("LEADERBOARD_DATA_REQUEST_FRIENDS"), p_handle);
	} else if (p_scope == "around_player") {
		// Ranks relative to the player's: half before, the rest after.
		steam()->call("downloadLeaderboardEntries", -(p_count / 2), p_count - p_count / 2 - 1, purr->steam_constant("LEADERBOARD_DATA_REQUEST_GLOBAL_AROUND_USER"), p_handle);
	} else {
		steam()->call("downloadLeaderboardEntries", 1, p_count, purr->steam_constant("LEADERBOARD_DATA_REQUEST_GLOBAL"), p_handle);
	}
}

void PurrLeaderboards::steam_scores_downloaded(const String &p_message, int64_t p_handle, const Array &p_entries) {
	if (!steam_downloads.has(p_handle) || steam_downloads[p_handle].is_empty()) {
		return; // The game's own download.
	}
	const SteamDownload download = steam_downloads[p_handle][0];
	steam_downloads[p_handle].remove_at(0);
	if (download.scope == "around_player" && p_entries.is_empty()) {
		// The player has no score: the best ones instead, as on the other platforms.
		steam_download(p_handle, "global", download.count, download.call);
		return;
	}
	const uint64_t player = (uint64_t)(int64_t)steam()->call("getSteamID");
	Array scores;
	for (int i = 0; i < MIN(p_entries.size(), download.count); i++) {
		const Dictionary entry = p_entries[i];
		const Variant id = entry.get("steam_id", 0);
		Dictionary score;
		score["rank"] = entry.get("global_rank", 0);
		score["name"] = steam()->call("getFriendPersonaName", id);
		score["score"] = entry.get("score", 0);
		score["player"] = (uint64_t)(int64_t)id == player;
		scores.push_back(score);
	}
	Dictionary result;
	result["ok"] = true;
	result["scores"] = scores;
	purr_complete(download.call, result);
}

// Steam has no leaderboard screen in its overlay: its community pages for the game's
// leaderboards open in it instead.
void PurrLeaderboards::steam_show(uint64_t p_handle) {
	const int64_t app_id = steam()->call("getAppID");
	const String url = p_handle ? vformat("https://steamcommunity.com/stats/%d/leaderboards/%s/", app_id, String::num_uint64(p_handle)) : vformat("https://steamcommunity.com/stats/%d/leaderboards/", app_id);
	steam()->call("activateGameOverlayToWebPage", url);
}
