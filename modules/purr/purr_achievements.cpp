#include "purr_achievements.h"

#include "achievements.h"
#include "purr.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"

namespace {

const char *SETTINGS_PREFIX = "achievements/";

} // namespace

Ref<PlayerSaves> PurrAchievements::player_saves() {
	const Ref<LocalPlayer> player = Purr::get_singleton()->get_player();
	return player.is_valid() ? player->get_saves() : Ref<PlayerSaves>();
}

Dictionary PurrAchievements::read(const Ref<PlayerSaves> &p_saves) {
	const Variant list = p_saves.is_valid() && p_saves->has(PURR_ACHIEVEMENTS_SLOT) ? p_saves->load(PURR_ACHIEVEMENTS_SLOT) : Variant();
	return list.get_type() == Variant::DICTIONARY ? Dictionary(list) : Dictionary();
}

bool PurrAchievements::record(const Ref<PlayerSaves> &p_saves, const String &p_id, double p_progress) {
	ERR_FAIL_COND_V_MSG(p_saves.is_null(), false, "A plain guest has no account to keep achievements on.");
	Dictionary list = read(p_saves);
	if (p_progress <= (double)list.get(p_id, 0.0)) {
		return false;
	}
	list[p_id] = p_progress;
	p_saves->save(PURR_ACHIEVEMENTS_SLOT, list, Dictionary());
	return true;
}

// achievements/<id>/<platform> in Project Settings, or the same ID.
String PurrAchievements::platform_name(const String &p_id) const {
	// Empty (as the Achievements tab in Project Settings leaves it): the same ID.
	const String name = ProjectSettings::get_singleton()->get_setting(SETTINGS_PREFIX + p_id + "/" + platform, String());
	return name.is_empty() ? p_id : name;
}

String PurrAchievements::purr_id(const String &p_platform_name) const {
	const HashMap<String, String>::ConstIterator id = ids.find(p_platform_name);
	return id ? id->value : p_platform_name;
}

void PurrAchievements::start() {
	Purr *purr = Purr::get_singleton();
	platform = purr->game_center_ready() ? "game_center" : purr->play_games_ready() ? "play_games" : purr->steam_ready() ? "steam" : "";
	loaded = false;
	on_platform.clear();
	total_steps.clear();
	ids.clear();
	if (platform.is_empty()) {
		return;
	}
	List<PropertyInfo> properties;
	ProjectSettings::get_singleton()->get_property_list(&properties);
	const String suffix = "/" + platform;
	for (const PropertyInfo &property : properties) {
		if (property.name.begins_with(SETTINGS_PREFIX) && property.name.ends_with(suffix)) {
			const String id = property.name.substr(strlen(SETTINGS_PREFIX), property.name.length() - strlen(SETTINGS_PREFIX) - suffix.length());
			const String name = GLOBAL_GET(property.name);
			ids[name.is_empty() ? id : name] = id;
		}
	}
	if (platform == "steam") {
		load_steam();
		return;
	}
	Signal loaded_signal = (platform == "game_center" ? purr->game_center() : purr->play_games())->call("get_achievements");
	loaded_signal.connect(callable_mp(this, &PurrAchievements::platform_loaded), CONNECT_ONE_SHOT);
}

// Steam's achievements can be read once the player's stats have arrived, which Steam says when
// asked for them.
void PurrAchievements::load_steam() {
	Object *steam = Purr::get_singleton()->steam();
	const Callable received = callable_mp(this, &PurrAchievements::steam_stats_received);
	if (!steam->is_connected(SNAME("user_stats_received"), received)) {
		steam->connect(SNAME("user_stats_received"), received);
	}
	steam->call("requestUserStats", steam->call("getSteamID"));
}

void PurrAchievements::steam_stats_received(int64_t p_game_id, int64_t p_result, int64_t p_user_id) {
	Purr *purr = Purr::get_singleton();
	Object *steam = purr->steam();
	if (platform != "steam" || p_user_id != (int64_t)steam->call("getSteamID")) {
		return;
	}
	Dictionary result;
	result["ok"] = p_result == purr->steam_constant("RESULT_OK");
	result["error"] = vformat("Steam result %d", p_result);
	Dictionary achievements;
	const int count = steam->call("getNumAchievements");
	for (int i = 0; i < count; i++) {
		const String name = steam->call("getAchievementName", i);
		const Dictionary achievement = steam->call("getAchievement", name);
		achievements[name] = achievement.get("achieved", false) ? 1.0 : 0.0;
	}
	result["achievements"] = achievements;
	platform_loaded(result);
}

// What the platform has: anything new goes into the slot, then the platform gets what it lacks.
void PurrAchievements::platform_loaded(const Dictionary &p_result) {
	if (!p_result.get("ok", false)) {
		// Sent anyway, in case the platform only failed to answer; it ignores what it has.
		WARN_PRINT(vformat("Couldn't load the player's achievements from %s: %s", platform, String(p_result.get("message", p_result.get("error", "")))));
	}
	on_platform.clear();
	total_steps.clear();
	for (const KeyValue<Variant, Variant> &entry : Dictionary(p_result.get("achievements", Dictionary()))) {
		double progress = 0.0;
		if (platform == "game_center") {
			progress = (double)entry.value / 100.0;
		} else if (platform == "play_games") {
			const Dictionary state = entry.value;
			const int64_t total = state.get("total_steps", 0);
			if (total > 0) {
				total_steps[entry.key] = total;
			}
			progress = state.get("unlocked", false) ? 1.0 : total > 0 ? (double)(int64_t)state.get("current_steps", 0) / total : 0.0;
		} else {
			progress = entry.value;
		}
		on_platform[entry.key] = CLAMP(progress, 0.0, 1.0);
	}

	const Ref<PlayerSaves> saves = player_saves();
	if (saves.is_valid()) {
		const Dictionary list = read(saves);
		Dictionary merged = list.duplicate();
		for (const KeyValue<Variant, Variant> &entry : on_platform) {
			const String id = purr_id(entry.key);
			if ((double)entry.value > (double)merged.get(id, 0.0)) {
				merged[id] = entry.value;
			}
		}
		if (merged != list) {
			saves->save(PURR_ACHIEVEMENTS_SLOT, merged, Dictionary());
			Achievements::get_singleton()->emit_signal(SNAME("changed"));
		}
	}
	loaded = true;
	send_missing();
}

void PurrAchievements::send_missing() {
	if (!loaded) {
		return;
	}
	for (const KeyValue<Variant, Variant> &entry : read(player_saves())) {
		send(entry.key, entry.value);
	}
}

void PurrAchievements::send(const String &p_id, double p_progress) {
	const String name = platform_name(p_id);
	if (platform.is_empty() || p_progress <= (double)on_platform.get(name, 0.0)) {
		return;
	}
	on_platform[name] = p_progress;
	Purr *purr = Purr::get_singleton();
	if (platform == "game_center") {
		purr->game_center()->call("report_achievement", name, p_progress * 100.0);
	} else if (platform == "play_games") {
		if (total_steps.has(name)) {
			purr->play_games()->call("set_achievement_steps", name, (int64_t)Math::round(p_progress * (int64_t)total_steps[name]));
		} else if (p_progress >= 1.0) {
			purr->play_games()->call("unlock_achievement", name);
		}
	} else if (p_progress >= 1.0) {
		// Steam keeps progress in stats (report() sets them); here it's only unlocking.
		purr->steam()->call("setAchievement", name);
		purr->steam()->call("storeStats");
	}
}

void PurrAchievements::report(const String &p_id, int64_t p_current, int64_t p_total) {
	ERR_FAIL_COND_MSG(p_total <= 0, "An achievement's total progress is above 0.");
	ERR_FAIL_COND_MSG(player_saves().is_null(), "Sign in (Account.sign_in) before unlocking achievements.");
	const double progress = CLAMP((double)p_current / p_total, 0.0, 1.0);
	if (record(player_saves(), p_id, progress)) {
		Achievements::get_singleton()->emit_signal(SNAME("changed"));
	}
	// On Steam, an achievement's progress is a stat (achievements/<id>/steam_stat), which
	// Steam unlocks the achievement from when it reaches the achievement's target.
	const String stat = ProjectSettings::get_singleton()->get_setting(SETTINGS_PREFIX + p_id + "/steam_stat", "");
	if (platform == "steam" && !stat.is_empty()) {
		Object *steam = Purr::get_singleton()->steam();
		steam->call("setStatInt", stat, p_current);
		steam->call("storeStats");
	}
	if (loaded) {
		send(p_id, progress);
	}
}

void PurrAchievements::show() {
	Purr *purr = Purr::get_singleton();
	if (platform == "steam") {
		purr->steam()->call("activateGameOverlay", "Achievements");
	} else if (platform == "game_center") {
		purr->game_center()->call("show_achievements");
	} else if (platform == "play_games") {
		purr->play_games()->call("show_achievements");
	} else {
		ERR_PRINT("Achievements have no screen without Steam, Game Center or Play Games; Achievements.get_all() has the player's list.");
	}
}
