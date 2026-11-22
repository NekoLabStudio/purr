#include "register_types.h"

#include "google_play_store.h"
#include "play_games.h"

#include "modules/purr/purr_store.h"

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
	GDREGISTER_INTERNAL_CLASS(GooglePlayStoreBackend);
	Ref<GooglePlayStoreBackend> store;
	store.instantiate();
	PurrStore::set_backend(store);
}

void uninitialize_play_games_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	PurrStore::set_backend(Ref<PurrStoreBackend>());
	memdelete(play_games);
	play_games = nullptr;
}
