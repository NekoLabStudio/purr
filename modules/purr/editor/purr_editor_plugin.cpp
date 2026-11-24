#include "purr_editor_plugin.h"

#include "platform_names_settings.h"

PurrEditorPlugin::PurrEditorPlugin() {
	Vector<Pair<String, String>> achievement_columns;
	achievement_columns.push_back({ "steam", TTR("Steam") });
	achievement_columns.push_back({ "steam_stat", TTR("Steam Stat (Progress)") });
	achievement_columns.push_back({ "game_center", TTR("Game Center") });
	achievement_columns.push_back({ "play_games", TTR("Play Games") });
	achievements = memnew(PlatformNamesSettings("achievements", achievement_columns, TTR("Empty: the same as the ID."), TTR("The ID the game uses, like first_delivery")));
	achievements->set_name(TTR("Achievements"));
	add_control_to_container(CONTAINER_PROJECT_SETTING_TAB_RIGHT, achievements);

	Vector<Pair<String, String>> leaderboard_columns;
	leaderboard_columns.push_back({ "steam", TTR("Steam") });
	leaderboard_columns.push_back({ "game_center", TTR("Game Center") });
	leaderboard_columns.push_back({ "play_games", TTR("Play Games") });
	// How the online services rank and reset the board, and the scores they take.
	leaderboard_columns.push_back({ "order", TTR("Order") });
	leaderboard_columns.push_back({ "reset", TTR("Resets") });
	leaderboard_columns.push_back({ "min", TTR("Lowest Score") });
	leaderboard_columns.push_back({ "max", TTR("Highest Score") });
	leaderboards = memnew(PlatformNamesSettings("leaderboards", leaderboard_columns, TTR("Empty: the same as the ID."), TTR("The ID the game uses, like weekly_tips")));
	leaderboards->set_column_choices("order", { { "higher", TTR("Higher First") }, { "lower", TTR("Lower First") } });
	leaderboards->set_column_choices("reset", { { "never", TTR("Never") }, { "daily", TTR("Daily") }, { "weekly", TTR("Weekly") }, { "monthly", TTR("Monthly") } });
	const String order_tooltip = TTR("Which scores rank first on the online services' leaderboard. Fixed by its first score: change it afterwards with npm run moderate.");
	leaderboards->set_column_tooltip("order", order_tooltip);
	leaderboards->set_column_tooltip("reset", TTR("When the online services' leaderboard starts again, at midnight UTC (weeks from Monday). Fixed by its first score."));
	leaderboards->set_column_tooltip("min", TTR("Empty: no limit. Scores below it are refused by the online services."));
	leaderboards->set_column_tooltip("max", TTR("Empty: no limit. Scores above it are refused by the online services."));
	leaderboards->set_name(TTR("Leaderboards"));
	add_control_to_container(CONTAINER_PROJECT_SETTING_TAB_RIGHT, leaderboards);

	Vector<Pair<String, String>> stat_columns;
	stat_columns.push_back({ "steam", TTR("Steam Stat") });
	stat_columns.push_back({ "max_per_day", TTR("Most a Day") });
	stats = memnew(PlatformNamesSettings("stats", stat_columns, TTR("Empty: none."), TTR("The ID the game uses, like km_driven")));
	stats->set_column_tooltip("steam", TTR("Empty: not on Steam. The Steam stat gets the player's value."));
	stats->set_column_tooltip("max_per_day", TTR("Empty: no limit. The most one player adds in a day on the online services; more doesn't count. Fixed by the stat's first change."));
	stats->set_name(TTR("Stats"));
	add_control_to_container(CONTAINER_PROJECT_SETTING_TAB_RIGHT, stats);

	Vector<Pair<String, String>> store_columns;
	store_columns.push_back({ "steam", TTR("Steam (DLC App ID)") });
	store_columns.push_back({ "apple", TTR("App Store (Product ID)") });
	store_columns.push_back({ "google_play", TTR("Google Play (Product ID)") });
	store = memnew(PlatformNamesSettings("store", store_columns, TTR("Empty: not sold on this store."), TTR("The ID the game uses, like winter_expansion")));
	store->set_name(TTR("Store"));
	add_control_to_container(CONTAINER_PROJECT_SETTING_TAB_RIGHT, store);
}

PurrEditorPlugin::~PurrEditorPlugin() {
	if (achievements) {
		remove_control_from_container(CONTAINER_PROJECT_SETTING_TAB_RIGHT, achievements);
		memdelete(achievements);
	}
	if (leaderboards) {
		remove_control_from_container(CONTAINER_PROJECT_SETTING_TAB_RIGHT, leaderboards);
		memdelete(leaderboards);
	}
	if (store) {
		remove_control_from_container(CONTAINER_PROJECT_SETTING_TAB_RIGHT, store);
		memdelete(store);
	}
}
