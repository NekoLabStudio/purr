#include "behavior_tree_editor.h"

#include "behavior_tree_debugger_plugin.h"

#include "core/object/callable_mp.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/gui/create_dialog.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/option_button.h"
#include "scene/gui/tree.h"

BehaviorTreeEditor::BehaviorTreeEditor() {
	set_name(TTRC("Behavior Tree"));
	set_icon_name("Tree");
	set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);
	set_available_layouts(EditorDock::DOCK_LAYOUT_HORIZONTAL | EditorDock::DOCK_LAYOUT_FLOATING);
	set_global(false);
	set_transient(true);

	VBoxContainer *column = memnew(VBoxContainer);
	add_child(column);

	HBoxContainer *toolbar = memnew(HBoxContainer);
	column->add_child(toolbar);
	add_button = memnew(Button);
	add_button->set_text(TTRC("Add Task"));
	add_button->set_flat(true);
	add_button->set_tooltip_text(TTRC("Add a task under the selected one, or as the root of an empty tree."));
	add_button->connect(SceneStringName(pressed), callable_mp(this, &BehaviorTreeEditor::_add_pressed));
	toolbar->add_child(add_button);
	up_button = memnew(Button);
	up_button->set_flat(true);
	up_button->set_tooltip_text(TTRC("Move Up"));
	up_button->connect(SceneStringName(pressed), callable_mp(this, &BehaviorTreeEditor::_move).bind(-1));
	toolbar->add_child(up_button);
	down_button = memnew(Button);
	down_button->set_flat(true);
	down_button->set_tooltip_text(TTRC("Move Down"));
	down_button->connect(SceneStringName(pressed), callable_mp(this, &BehaviorTreeEditor::_move).bind(1));
	toolbar->add_child(down_button);
	remove_button = memnew(Button);
	remove_button->set_flat(true);
	remove_button->set_tooltip_text(TTRC("Remove the selected task and the tasks under it."));
	remove_button->connect(SceneStringName(pressed), callable_mp(this, &BehaviorTreeEditor::_remove_pressed));
	toolbar->add_child(remove_button);
	toolbar->add_spacer();
	// While the game runs: which character's tree the statuses are from.
	agent_picker = memnew(OptionButton);
	agent_picker->set_tooltip_text(TTRC("The character whose tasks' statuses are shown while the game runs."));
	agent_picker->set_fit_to_longest_item(false);
	agent_picker->hide();
	agent_picker->connect(SceneStringName(item_selected), callable_mp(this, &BehaviorTreeEditor::_apply_live).unbind(1));
	toolbar->add_child(agent_picker);

	outline = memnew(Tree);
	outline->set_v_size_flags(SIZE_EXPAND_FILL);
	outline->set_columns(2);
	outline->set_column_expand(1, false);
	outline->set_column_custom_minimum_width(1, 110 * EDSCALE);
	outline->set_hide_root(false);
	outline->set_allow_reselect(true);
	outline->set_drop_mode_flags(Tree::DROP_MODE_ON_ITEM | Tree::DROP_MODE_INBETWEEN);
	outline->connect(SceneStringName(item_selected), callable_mp(this, &BehaviorTreeEditor::_selected_changed));
	outline->connect("nothing_selected", callable_mp(this, &BehaviorTreeEditor::_update_buttons));
	SET_DRAG_FORWARDING_GCD(outline, BehaviorTreeEditor);
	column->add_child(outline);

	empty_label = memnew(Label);
	empty_label->set_text(TTRC("This tree has no tasks yet. Add one to be its root."));
	empty_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	empty_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	empty_label->set_v_size_flags(SIZE_EXPAND_FILL);
	empty_label->set_theme_type_variation("HeaderSmall");
	empty_label->hide();
	column->add_child(empty_label);

	create_dialog = memnew(CreateDialog);
	create_dialog->set_base_type("BehaviorTask");
	create_dialog->connect("create", callable_mp(this, &BehaviorTreeEditor::_create_confirmed));
	add_child(create_dialog);
}

void BehaviorTreeEditor::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED:
			add_button->set_button_icon(get_editor_theme_icon(SNAME("Add")));
			up_button->set_button_icon(get_editor_theme_icon(SNAME("MoveUp")));
			down_button->set_button_icon(get_editor_theme_icon(SNAME("MoveDown")));
			remove_button->set_button_icon(get_editor_theme_icon(SNAME("Remove")));
			break;
	}
}

void BehaviorTreeEditor::edit(const Ref<BehaviorTree> &p_tree) {
	if (behavior_tree == p_tree) {
		return;
	}
	if (behavior_tree.is_valid()) {
		behavior_tree->disconnect_changed(callable_mp(this, &BehaviorTreeEditor::_queue_refresh));
	}
	behavior_tree = p_tree;
	if (behavior_tree.is_valid()) {
		behavior_tree->connect_changed(callable_mp(this, &BehaviorTreeEditor::_queue_refresh));
	}
	clear_live();
	if (debugger != nullptr) {
		debugger->watch(_tree_path());
	}
	_refresh();
}

String BehaviorTreeEditor::_tree_path() const {
	return behavior_tree.is_valid() ? behavior_tree->get_path() : String();
}

// A report from the running game: the statuses of one character's copy of the tree.
void BehaviorTreeEditor::show_live(const String &p_tree_path, const String &p_player_path, const String &p_agent_name, const PackedInt32Array &p_statuses) {
	if (p_tree_path != _tree_path()) {
		return;
	}
	if (!live.has(p_player_path)) {
		agent_picker->add_item(p_agent_name);
		agent_picker->set_item_metadata(agent_picker->get_item_count() - 1, p_player_path);
		agent_picker->set_item_tooltip(agent_picker->get_item_count() - 1, p_player_path);
		agent_picker->show();
		if (agent_picker->get_selected() < 0) {
			agent_picker->select(0);
		}
	}
	live[p_player_path] = p_statuses;
	_apply_live();
}

void BehaviorTreeEditor::clear_live() {
	live.clear();
	agent_picker->clear();
	agent_picker->hide();
	_apply_live();
}

// Each task's status beside it, in words and colour, for the chosen character.
void BehaviorTreeEditor::_apply_live() {
	const int picked = agent_picker->get_selected();
	const String agent = picked >= 0 ? String(agent_picker->get_item_metadata(picked)) : String();
	const PackedInt32Array statuses = live.has(agent) ? live[agent] : PackedInt32Array();
	const Color running = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color success = get_theme_color(SNAME("success_color"), EditorStringName(Editor));
	const Color failure = get_theme_color(SNAME("error_color"), EditorStringName(Editor));
	int index = 0;
	for (TreeItem *item = outline->get_root(); item != nullptr; item = item->get_next_in_tree(), index++) {
		item->clear_custom_color(0);
		item->set_text(1, String());
		if (index >= statuses.size()) {
			continue;
		}
		switch (BehaviorTask::Status(statuses[index])) {
			case BehaviorTask::RUNNING:
				item->set_text(1, TTR("Running"));
				item->set_custom_color(0, running);
				item->set_custom_color(1, running);
				break;
			case BehaviorTask::SUCCESS:
				item->set_text(1, TTR("Succeeded"));
				item->set_custom_color(1, success);
				break;
			case BehaviorTask::FAILURE:
				item->set_text(1, TTR("Failed"));
				item->set_custom_color(1, failure);
				break;
			case BehaviorTask::IDLE:
				break;
		}
	}
}

void BehaviorTreeEditor::_queue_refresh() {
	if (!refresh_queued) {
		refresh_queued = true;
		callable_mp(this, &BehaviorTreeEditor::_refresh).call_deferred();
	}
}

void BehaviorTreeEditor::_watch(const Ref<BehaviorTask> &p_task) {
	if (!p_task->is_connected(CoreStringName(changed), callable_mp(this, &BehaviorTreeEditor::_queue_refresh))) {
		p_task->connect_changed(callable_mp(this, &BehaviorTreeEditor::_queue_refresh));
	}
	watched.push_back(p_task);
}

void BehaviorTreeEditor::_unwatch_all() {
	for (const Ref<BehaviorTask> &task : watched) {
		if (task->is_connected(CoreStringName(changed), callable_mp(this, &BehaviorTreeEditor::_queue_refresh))) {
			task->disconnect_changed(callable_mp(this, &BehaviorTreeEditor::_queue_refresh));
		}
	}
	watched.clear();
}

// Rebuilt from the tree, keeping the same task selected and branches folded as they were.
void BehaviorTreeEditor::_refresh() {
	refresh_queued = false;
	const Ref<BehaviorTask> selected = _selected();
	HashSet<ObjectID> folded;
	if (outline->get_root() != nullptr) {
		for (TreeItem *item = outline->get_root(); item != nullptr; item = item->get_next_in_tree()) {
			if (item->is_collapsed()) {
				folded.insert(ObjectID(uint64_t(item->get_metadata(0))));
			}
		}
	}
	_unwatch_all();
	outline->clear();
	const bool empty = behavior_tree.is_null() || behavior_tree->get_root().is_null();
	outline->set_visible(!empty);
	empty_label->set_visible(behavior_tree.is_valid() && empty);
	if (!empty) {
		_add_item(nullptr, behavior_tree->get_root());
		for (TreeItem *item = outline->get_root(); item != nullptr; item = item->get_next_in_tree()) {
			const ObjectID id = ObjectID(uint64_t(item->get_metadata(0)));
			item->set_collapsed(folded.has(id));
			if (selected.is_valid() && id == selected->get_instance_id()) {
				item->select(0);
			}
		}
	}
	_apply_live();
	_update_buttons();
}

void BehaviorTreeEditor::_add_item(TreeItem *p_parent, const Ref<BehaviorTask> &p_task) {
	TreeItem *item = outline->create_item(p_parent);
	if (p_task.is_null()) {
		item->set_text(0, TTR("(empty)"));
		item->set_metadata(0, uint64_t(0));
		return;
	}
	_watch(p_task);
	item->set_text(0, p_task->get_display_title());
	item->set_metadata(0, uint64_t(p_task->get_instance_id()));
	const Ref<Script> script = p_task->get_script();
	item->set_tooltip_text(0, script.is_valid() ? script->get_path() : p_task->get_class());
	item->set_icon(0, EditorNode::get_singleton()->get_object_icon(p_task.ptr(), "Object"));
	for (int i = 0; i < p_task->get_child_count(); i++) {
		_add_item(item, p_task->get_child(i));
	}
}

Ref<BehaviorTask> BehaviorTreeEditor::_task_of(TreeItem *p_item) const {
	if (p_item == nullptr) {
		return Ref<BehaviorTask>();
	}
	return Ref<BehaviorTask>(Object::cast_to<BehaviorTask>(ObjectDB::get_instance(ObjectID(uint64_t(p_item->get_metadata(0))))));
}

Ref<BehaviorTask> BehaviorTreeEditor::_selected() const {
	return _task_of(outline->get_selected());
}

Ref<BehaviorTask> BehaviorTreeEditor::_find_parent(const Ref<BehaviorTask> &p_from, const Ref<BehaviorTask> &p_task) const {
	if (p_from.is_null()) {
		return Ref<BehaviorTask>();
	}
	for (int i = 0; i < p_from->get_child_count(); i++) {
		const Ref<BehaviorTask> child = p_from->get_child(i);
		if (child == p_task) {
			return p_from;
		}
		const Ref<BehaviorTask> found = _find_parent(child, p_task);
		if (found.is_valid()) {
			return found;
		}
	}
	return Ref<BehaviorTask>();
}

Ref<BehaviorTask> BehaviorTreeEditor::_parent_of(const Ref<BehaviorTask> &p_task) const {
	return behavior_tree.is_valid() ? _find_parent(behavior_tree->get_root(), p_task) : Ref<BehaviorTask>();
}

bool BehaviorTreeEditor::_contains(const Ref<BehaviorTask> &p_ancestor, const Ref<BehaviorTask> &p_task) const {
	if (p_ancestor == p_task) {
		return true;
	}
	for (int i = 0; p_ancestor.is_valid() && i < p_ancestor->get_child_count(); i++) {
		if (_contains(p_ancestor->get_child(i), p_task)) {
			return true;
		}
	}
	return false;
}

void BehaviorTreeEditor::_update_buttons() {
	const Ref<BehaviorTask> selected = _selected();
	const Ref<BehaviorTask> parent = selected.is_valid() ? _parent_of(selected) : Ref<BehaviorTask>();
	const int index = parent.is_valid() ? parent->get_children().find(selected) : -1;
	const bool empty = behavior_tree.is_valid() && behavior_tree->get_root().is_null();
	add_button->set_disabled(behavior_tree.is_null() || (!empty && selected.is_null()));
	remove_button->set_disabled(selected.is_null());
	up_button->set_disabled(index <= 0);
	down_button->set_disabled(index < 0 || index >= parent->get_child_count() - 1);
}

void BehaviorTreeEditor::_selected_changed() {
	_update_buttons();
	const Ref<BehaviorTask> selected = _selected();
	if (selected.is_valid()) {
		// In the inspector only, so this dock stays on the tree.
		EditorInterface::get_singleton()->inspect_object(selected.ptr(), String(), true);
	}
}

// One undoable change: a task's children, or the tree's root when there's no parent.
void BehaviorTreeEditor::_set_children(const Ref<BehaviorTask> &p_parent, const TypedArray<BehaviorTask> &p_children, const String &p_action) {
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(p_action, UndoRedo::MERGE_DISABLE, behavior_tree.ptr());
	undo_redo->add_do_method(p_parent.ptr(), "set_children", p_children);
	undo_redo->add_undo_method(p_parent.ptr(), "set_children", p_parent->get_children().duplicate());
	undo_redo->commit_action();
}

void BehaviorTreeEditor::_add_pressed() {
	create_dialog->popup_create(true);
}

void BehaviorTreeEditor::_create_confirmed() {
	Ref<BehaviorTask> task = Object::cast_to<BehaviorTask>(create_dialog->instantiate_selected().get_validated_object());
	if (task.is_null() || behavior_tree.is_null()) {
		return;
	}
	if (behavior_tree->get_root().is_null()) {
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->create_action(TTR("Add Root Task"), UndoRedo::MERGE_DISABLE, behavior_tree.ptr());
		undo_redo->add_do_property(behavior_tree.ptr(), "root", task);
		undo_redo->add_undo_property(behavior_tree.ptr(), "root", Ref<BehaviorTask>());
		undo_redo->commit_action();
		return;
	}
	const Ref<BehaviorTask> parent = _selected();
	if (parent.is_null()) {
		return;
	}
	TypedArray<BehaviorTask> children = parent->get_children().duplicate();
	children.push_back(task);
	_set_children(parent, children, TTR("Add Task"));
}

void BehaviorTreeEditor::_remove_pressed() {
	const Ref<BehaviorTask> selected = _selected();
	if (selected.is_null()) {
		return;
	}
	const Ref<BehaviorTask> parent = _parent_of(selected);
	if (parent.is_null()) {
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->create_action(TTR("Remove Root Task"), UndoRedo::MERGE_DISABLE, behavior_tree.ptr());
		undo_redo->add_do_property(behavior_tree.ptr(), "root", Ref<BehaviorTask>());
		undo_redo->add_undo_property(behavior_tree.ptr(), "root", selected);
		undo_redo->commit_action();
		return;
	}
	TypedArray<BehaviorTask> children = parent->get_children().duplicate();
	children.erase(selected);
	_set_children(parent, children, TTR("Remove Task"));
}

void BehaviorTreeEditor::_move(int p_by) {
	const Ref<BehaviorTask> selected = _selected();
	const Ref<BehaviorTask> parent = selected.is_valid() ? _parent_of(selected) : Ref<BehaviorTask>();
	if (parent.is_null()) {
		return;
	}
	TypedArray<BehaviorTask> children = parent->get_children().duplicate();
	const int from = children.find(selected);
	const int to = from + p_by;
	if (from < 0 || to < 0 || to >= children.size()) {
		return;
	}
	children.remove_at(from);
	children.insert(to, selected);
	_set_children(parent, children, p_by < 0 ? TTR("Move Task Up") : TTR("Move Task Down"));
}

Variant BehaviorTreeEditor::get_drag_data_fw(const Point2 &p_point, Control *p_from) {
	TreeItem *item = outline->get_item_at_position(p_point);
	const Ref<BehaviorTask> task = _task_of(item);
	if (task.is_null() || _parent_of(task).is_null()) {
		return Variant(); // The root stays the root.
	}
	Label *preview = memnew(Label);
	preview->set_text(task->get_display_title());
	set_drag_preview(preview);
	Dictionary data;
	data["type"] = "behavior_task";
	data["task"] = task;
	return data;
}

bool BehaviorTreeEditor::can_drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) const {
	const Dictionary data = p_data;
	if (data.get("type", "") != "behavior_task") {
		return false;
	}
	const Ref<BehaviorTask> dragged = data["task"];
	TreeItem *item = outline->get_item_at_position(p_point);
	const Ref<BehaviorTask> target = _task_of(item);
	if (dragged.is_null() || target.is_null()) {
		return false;
	}
	// Not into itself or anything under it, and not beside the root.
	const int section = outline->get_drop_section_at_position(p_point);
	if (section != 0 && _parent_of(target).is_null()) {
		return false;
	}
	return !_contains(dragged, target);
}

// Onto a task: its last child. Above or below one: beside it.
void BehaviorTreeEditor::drop_data_fw(const Point2 &p_point, const Variant &p_data, Control *p_from) {
	if (!can_drop_data_fw(p_point, p_data, p_from)) {
		return;
	}
	const Ref<BehaviorTask> dragged = Dictionary(p_data)["task"];
	const Ref<BehaviorTask> target = _task_of(outline->get_item_at_position(p_point));
	const int section = outline->get_drop_section_at_position(p_point);
	const Ref<BehaviorTask> old_parent = _parent_of(dragged);
	const Ref<BehaviorTask> new_parent = section == 0 ? target : _parent_of(target);

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Move Task"), UndoRedo::MERGE_DISABLE, behavior_tree.ptr());
	TypedArray<BehaviorTask> old_children = old_parent->get_children().duplicate();
	old_children.erase(dragged);
	TypedArray<BehaviorTask> new_children = new_parent == old_parent ? old_children : new_parent->get_children().duplicate();
	if (section == 0) {
		new_children.push_back(dragged);
	} else {
		const int at = new_children.find(target) + (section > 0 ? 1 : 0);
		new_children.insert(at, dragged);
	}
	if (new_parent != old_parent) {
		undo_redo->add_do_method(old_parent.ptr(), "set_children", old_children);
		undo_redo->add_undo_method(old_parent.ptr(), "set_children", old_parent->get_children().duplicate());
	}
	undo_redo->add_do_method(new_parent.ptr(), "set_children", new_children);
	undo_redo->add_undo_method(new_parent.ptr(), "set_children", new_parent->get_children().duplicate());
	undo_redo->commit_action();
}
