#include "behavior_tree_editor_plugin.h"

#include "../behavior_tree_player.h"
#include "behavior_tree_debugger_plugin.h"
#include "behavior_tree_editor.h"

#include "editor/docks/editor_dock_manager.h"
#include "editor/themes/editor_scale.h"

BehaviorTreeEditorPlugin::BehaviorTreeEditorPlugin() {
	editor = memnew(BehaviorTreeEditor);
	editor->set_custom_minimum_size(Size2(0, 250) * EDSCALE);
	EditorDockManager::get_singleton()->add_dock(editor);
	editor->close();
	debugger.instantiate();
	debugger->set_editor(editor);
	editor->set_debugger(debugger.ptr());
	add_debugger_plugin(debugger);
}

BehaviorTreeEditorPlugin::~BehaviorTreeEditorPlugin() {
	if (debugger.is_valid()) {
		debugger->set_editor(nullptr);
	}
}

// A task being inspected keeps the tree it was picked from open.
bool BehaviorTreeEditorPlugin::handles(Object *p_object) const {
	if (Object::cast_to<BehaviorTree>(p_object) != nullptr) {
		return true;
	}
	if (const BehaviorTreePlayer *player = Object::cast_to<BehaviorTreePlayer>(p_object)) {
		return player->get_behavior_tree().is_valid();
	}
	return Object::cast_to<BehaviorTask>(p_object) != nullptr && editor->get_edited().is_valid();
}

void BehaviorTreeEditorPlugin::edit(Object *p_object) {
	if (BehaviorTree *tree = Object::cast_to<BehaviorTree>(p_object)) {
		editor->edit(Ref<BehaviorTree>(tree));
	} else if (BehaviorTreePlayer *player = Object::cast_to<BehaviorTreePlayer>(p_object)) {
		editor->edit(player->get_behavior_tree());
	}
}

void BehaviorTreeEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		editor->make_visible();
	} else {
		editor->close();
	}
}
