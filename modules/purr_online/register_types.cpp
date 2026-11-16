#include "register_types.h"

#include "purr_online.h"

#include "core/object/class_db.h"

static PurrOnline *purr_online = nullptr;

void initialize_purr_online_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	// The online service's client, for the purr module: games use Account, Saves, Lobbies and the rest.
	GDREGISTER_INTERNAL_CLASS(PurrOnline);
	GDREGISTER_INTERNAL_CLASS(PurrOnlineCall);
	purr_online = memnew(PurrOnline);
}

void uninitialize_purr_online_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	memdelete(purr_online);
	purr_online = nullptr;
}
