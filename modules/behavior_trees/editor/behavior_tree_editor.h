#pragma once

// The Behavior Tree dock: the tree being edited as an outline. Add a task (any built-in task or
// a script extending BehaviorTask) under the selected one, remove it, move it up or down, or drag
// it somewhere else. Selecting a task shows it in the inspector. Every change can be undone.

#include "../behavior_tree.h"

#include "editor/docks/editor_dock.h"

class BehaviorTreeDebuggerPlugin;
class Button;
class CreateDialog;
class Label;
class OptionButton;
class Tree;
class TreeItem;

class BehaviorTreeEditor : public EditorDock {
	GDCLASS(BehaviorTreeEditor, EditorDock);

	Ref<BehaviorTree> behavior_tree;
	Vector<Ref<BehaviorTask>> watched; // Tasks whose changes refresh the outline.

	Tree *outline = nullptr;
	Label *empty_label = nullptr;
	Button *add_button = nullptr;
	Button *remove_button = nullptr;
	Button *up_button = nullptr;
	Button *down_button = nullptr;
	CreateDialog *create_dialog = nullptr;
	OptionButton *agent_picker = nullptr;
	bool refresh_queued = false;

	BehaviorTreeDebuggerPlugin *debugger = nullptr;
	HashMap<String, PackedInt32Array> live; // Each running agent's task statuses, by its path.

	void _queue_refresh();
	void _refresh();
	void _add_item(TreeItem *p_parent, const Ref<BehaviorTask> &p_task);
	void _watch(const Ref<BehaviorTask> &p_task);
	void _unwatch_all();
	void _update_buttons();
	void _apply_live();
	String _tree_path() const;

	Ref<BehaviorTask> _task_of(TreeItem *p_item) const;
	Ref<BehaviorTask> _selected() const;
	Ref<BehaviorTask> _parent_of(const Ref<BehaviorTask> &p_task) const;
	Ref<BehaviorTask> _find_parent(const Ref<BehaviorTask> &p_from, const Ref<BehaviorTask> &p_task) const;
	bool _contains(const Ref<BehaviorTask> &p_ancestor, const Ref<BehaviorTask> &p_task) const;

	void _set_children(const Ref<BehaviorTask> &p_parent, const TypedArray<BehaviorTask> &p_children, const String &p_action);
	void _add_pressed();
	void _create_confirmed();
	void _remove_pressed();
	void _move(int p_by);
	void _selected_changed();

	Variant get_drag_data_fw(const Point2 &p_point, Control *p_from);
	bool can_drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) const;
	void drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from);

protected:
	void _notification(int p_what);

public:
	void edit(const Ref<BehaviorTree> &p_tree);
	void set_debugger(BehaviorTreeDebuggerPlugin *p_debugger) { debugger = p_debugger; }
	void show_live(const String &p_tree_path, const String &p_player_path, const String &p_agent_name, const PackedInt32Array &p_statuses);
	void clear_live();
	Ref<BehaviorTree> get_edited() const { return behavior_tree; }

	BehaviorTreeEditor();
};
