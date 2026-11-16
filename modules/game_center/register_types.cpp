#include "register_types.h"

#include "game_center.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

static GameCenter *game_center = nullptr;

void initialize_game_center_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(GameCenter);
	GDREGISTER_INTERNAL_CLASS(GameCenterCall);
	game_center = memnew(GameCenter);
	Engine::get_singleton()->add_singleton(Engine::Singleton("GameCenter", GameCenter::get_singleton()));
}

void uninitialize_game_center_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	memdelete(game_center);
	game_center = nullptr;
}
