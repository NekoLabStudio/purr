#include "register_types.h"

#include "behavior_call_method.h"
#include "behavior_cooldown.h"
#include "behavior_inverter.h"
#include "behavior_parallel.h"
#include "behavior_repeat.h"
#include "behavior_selector.h"
#include "behavior_sequence.h"
#include "behavior_state.h"
#include "behavior_state_machine.h"
#include "behavior_task.h"
#include "behavior_time_limit.h"
#include "behavior_tree.h"
#include "behavior_tree_debugger.h"
#include "behavior_tree_player.h"
#include "behavior_wait.h"

#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "editor/behavior_tree_debugger_plugin.h"
#include "editor/behavior_tree_editor.h"
#include "editor/behavior_tree_editor_plugin.h"
#include "editor/plugins/editor_plugin.h"
#endif

void initialize_behavior_trees_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(BehaviorTask);
		GDREGISTER_CLASS(BehaviorSequence);
		GDREGISTER_CLASS(BehaviorSelector);
		GDREGISTER_CLASS(BehaviorParallel);
		GDREGISTER_CLASS(BehaviorInverter);
		GDREGISTER_CLASS(BehaviorRepeat);
		GDREGISTER_CLASS(BehaviorCooldown);
		GDREGISTER_CLASS(BehaviorTimeLimit);
		GDREGISTER_CLASS(BehaviorWait);
		GDREGISTER_CLASS(BehaviorCallMethod);
		GDREGISTER_CLASS(BehaviorTree);
		GDREGISTER_CLASS(BehaviorTreePlayer);
		GDREGISTER_CLASS(BehaviorState);
		GDREGISTER_CLASS(BehaviorStateMachine);
#ifdef DEBUG_ENABLED
		BehaviorTreeDebugger::initialize();
#endif
	}
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_INTERNAL_CLASS(BehaviorTreeEditor);
		GDREGISTER_INTERNAL_CLASS(BehaviorTreeDebuggerPlugin);
		GDREGISTER_INTERNAL_CLASS(BehaviorTreeEditorPlugin);
		EditorPlugins::add_by_type<BehaviorTreeEditorPlugin>();
	}
#endif
}

void uninitialize_behavior_trees_module(ModuleInitializationLevel p_level) {
#ifdef DEBUG_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		BehaviorTreeDebugger::finalize();
	}
#endif
}
