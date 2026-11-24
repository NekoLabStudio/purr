#include "register_types.h"

#include "account.h"
#include "app_watcher.h"
#include "achievements.h"
#include "feedback.h"
#include "friends.h"
#include "leaderboards.h"
#include "live_config.h"
#include "lobbies.h"
#include "local_player.h"
#include "local_players.h"
#include "player_saves.h"
#include "purr.h"
#include "saves.h"
#include "stats.h"
#include "store.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "editor/export/editor_export.h"
#include "editor/plugins/editor_plugin.h"
#include "editor/purr_editor_plugin.h"
#include "editor/purr_export_plugin.h"
#include "editor/platform_names_settings.h"

static void _editor_init() {
	Ref<PurrExportPlugin> export_plugin;
	export_plugin.instantiate();
	EditorExport::get_singleton()->add_export_plugin(export_plugin);
}
#endif

static Purr *purr = nullptr;
// The singletons games use, each over Purr.
static Object *singletons[11] = {};

void initialize_purr_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_INTERNAL_CLASS(Purr);
		GDREGISTER_INTERNAL_CLASS(PurrLobby);
		GDREGISTER_INTERNAL_CLASS(PurrLobbyReferee);
		GDREGISTER_INTERNAL_CLASS(PurrPlatformLobby);
		GDREGISTER_INTERNAL_CLASS(PurrSteamLobby);
		GDREGISTER_INTERNAL_CLASS(PurrGameCenterLobby);
		GDREGISTER_INTERNAL_CLASS(PurrFriends);
		GDREGISTER_INTERNAL_CLASS(PurrAchievements);
		GDREGISTER_INTERNAL_CLASS(PurrLeaderboards);
		GDREGISTER_INTERNAL_CLASS(PurrStore);
		GDREGISTER_INTERNAL_CLASS(PurrStoreBackend);
		GDREGISTER_ABSTRACT_CLASS(LocalPlayer);
		GDREGISTER_ABSTRACT_CLASS(PlayerSaves);
		GDREGISTER_ABSTRACT_CLASS(Account);
		GDREGISTER_ABSTRACT_CLASS(LocalPlayers);
		GDREGISTER_ABSTRACT_CLASS(Saves);
		GDREGISTER_ABSTRACT_CLASS(Achievements);
		GDREGISTER_ABSTRACT_CLASS(Leaderboards);
		GDREGISTER_ABSTRACT_CLASS(Lobbies);
		GDREGISTER_ABSTRACT_CLASS(Friends);
		GDREGISTER_ABSTRACT_CLASS(Store);
		GDREGISTER_ABSTRACT_CLASS(LiveConfig);
		GDREGISTER_ABSTRACT_CLASS(Feedback);
		GDREGISTER_ABSTRACT_CLASS(Stats);
		GDREGISTER_INTERNAL_CLASS(PurrAppWatcher);
		singletons[0] = memnew(Account);
		singletons[1] = memnew(LocalPlayers);
		singletons[2] = memnew(Saves);
		singletons[3] = memnew(Achievements);
		singletons[4] = memnew(Leaderboards);
		singletons[5] = memnew(Lobbies);
		singletons[6] = memnew(Friends);
		singletons[7] = memnew(Store);
		singletons[8] = memnew(LiveConfig);
		singletons[9] = memnew(Feedback);
		singletons[10] = memnew(Stats);
		for (Object *singleton : singletons) {
			Engine::get_singleton()->add_singleton(Engine::Singleton(singleton->get_class(), singleton));
		}
		purr = memnew(Purr);
	}
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_INTERNAL_CLASS(PlatformNamesSettings);
		GDREGISTER_INTERNAL_CLASS(PurrEditorPlugin);
		EditorPlugins::add_by_type<PurrEditorPlugin>();
		EditorNode::add_init_callback(_editor_init);
	}
#endif
}

void uninitialize_purr_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		memdelete(purr);
		purr = nullptr;
		for (Object *&singleton : singletons) {
			memdelete(singleton);
			singleton = nullptr;
		}
	}
}
