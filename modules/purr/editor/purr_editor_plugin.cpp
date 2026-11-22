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
	leaderboards = memnew(PlatformNamesSettings("leaderboards", leaderboard_columns, TTR("Empty: the same as the ID."), TTR("The ID the game uses, like weekly_tips")));
	leaderboards->set_name(TTR("Leaderboards"));
	add_control_to_container(CONTAINER_PROJECT_SETTING_TAB_RIGHT, leaderboards);

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
