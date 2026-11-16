#include "register_types.h"

#include "play_games.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

static PlayGames *play_games = nullptr;

void initialize_play_games_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(PlayGames);
	GDREGISTER_INTERNAL_CLASS(PlayGamesCall);
	play_games = memnew(PlayGames);
	Engine::get_singleton()->add_singleton(Engine::Singleton("PlayGames", PlayGames::get_singleton()));
}

void uninitialize_play_games_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	memdelete(play_games);
	play_games = nullptr;
}
