#include "platform_names_settings.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/settings/project_settings_editor.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/tree.h"

// IDs a platform takes: letters, digits, _, . and -.
static bool valid_id(const String &p_id) {
	if (p_id.is_empty()) {
		return false;
	}
	for (int i = 0; i < p_id.length(); i++) {
		const char32_t c = p_id[i];
		if (!is_ascii_alphanumeric_char(c) && c != '_' && c != '.' && c != '-') {
			return false;
		}
	}
	return true;
}

Vector<String> PlatformNamesSettings::ids() const {
	List<PropertyInfo> properties;
	ProjectSettings::get_singleton()->get_property_list(&properties);
	Vector<String> found;
	const String start = prefix + "/";
	for (const PropertyInfo &property : properties) {
		if (!property.name.begins_with(start)) {
			continue;
		}
		// <prefix>/<id>/<platform>: a setting straight under the prefix (store/owned_in_editor) isn't
		// an item.
		const String rest = property.name.substr(start.length());
		const String id = rest.get_slicec('/', 0);
		if (rest.contains("/") && !id.is_empty() && !found.has(id)) {
			found.push_back(id);
		}
	}
	found.sort();
	return found;
}

void PlatformNamesSettings::update_list() {
	updating = true;
	tree->clear();
	TreeItem *root = tree->create_item();
	for (const String &id : ids()) {
		TreeItem *item = tree->create_item(root);
		item->set_text(0, id);
		item->set_metadata(0, id);
		for (int i = 0; i < columns.size(); i++) {
			const String key = prefix + "/" + id + "/" + columns[i];
			item->set_text(i + 1, ProjectSettings::get_singleton()->get_setting(key, String()));
			item->set_editable(i + 1, true);
			item->set_tooltip_text(i + 1, empty_tooltip);
		}
		item->add_button(columns.size() + 1, get_editor_theme_icon(SNAME("Remove")), 0, false, TTR("Remove"));
	}
	updating = false;
}

void PlatformNamesSettings::changed() {
	ProjectSettingsEditor::get_singleton()->queue_save();
}

void PlatformNamesSettings::add_pressed() {
	const String id = id_edit->get_text().strip_edges();
	if (!valid_id(id) || ids().has(id)) {
		return;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(vformat(TTR("Add %s"), id));
	for (const String &column : columns) {
		const String key = prefix + "/" + id + "/" + column;
		undo_redo->add_do_property(ProjectSettings::get_singleton(), key, String());
		undo_redo->add_undo_property(ProjectSettings::get_singleton(), key, Variant());
	}
	undo_redo->add_do_method(this, "update_list");
	undo_redo->add_undo_method(this, "update_list");
	undo_redo->add_do_method(this, "changed");
	undo_redo->add_undo_method(this, "changed");
	undo_redo->commit_action();
	id_edit->clear();
}

void PlatformNamesSettings::item_edited() {
	if (updating) {
		return;
	}
	TreeItem *item = tree->get_edited();
	const int column = tree->get_edited_column();
	if (!item || column < 1 || column > columns.size()) {
		return;
	}
	const String key = prefix + "/" + String(item->get_metadata(0)) + "/" + columns[column - 1];
	const String name = item->get_text(column).strip_edges();
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Change Platform Name"));
	undo_redo->add_do_property(ProjectSettings::get_singleton(), key, name);
	undo_redo->add_undo_property(ProjectSettings::get_singleton(), key, ProjectSettings::get_singleton()->get_setting(key, String()));
	undo_redo->add_do_method(this, "update_list");
	undo_redo->add_undo_method(this, "update_list");
	undo_redo->add_do_method(this, "changed");
	undo_redo->add_undo_method(this, "changed");
	undo_redo->commit_action();
}

void PlatformNamesSettings::button_clicked(Object *p_item, int p_column, int p_id, MouseButton p_button) {
	TreeItem *item = Object::cast_to<TreeItem>(p_item);
	if (!item || p_button != MouseButton::LEFT) {
		return;
	}
	const String id = item->get_metadata(0);
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(vformat(TTR("Remove %s"), id));
	for (const String &column : columns) {
		const String key = prefix + "/" + id + "/" + column;
		if (ProjectSettings::get_singleton()->has_setting(key)) {
			undo_redo->add_do_property(ProjectSettings::get_singleton(), key, Variant());
			undo_redo->add_undo_property(ProjectSettings::get_singleton(), key, ProjectSettings::get_singleton()->get_setting(key));
		}
	}
	undo_redo->add_do_method(this, "update_list");
	undo_redo->add_undo_method(this, "update_list");
	undo_redo->add_do_method(this, "changed");
	undo_redo->add_undo_method(this, "changed");
	undo_redo->commit_action();
}

void PlatformNamesSettings::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE || p_what == NOTIFICATION_VISIBILITY_CHANGED) {
		if (is_visible_in_tree()) {
			update_list();
		}
	}
}

void PlatformNamesSettings::_bind_methods() {
	ClassDB::bind_method(D_METHOD("update_list"), &PlatformNamesSettings::update_list);
	ClassDB::bind_method(D_METHOD("changed"), &PlatformNamesSettings::changed);
}

PlatformNamesSettings::PlatformNamesSettings(const String &p_prefix, const Vector<Pair<String, String>> &p_columns, const String &p_empty_tooltip, const String &p_id_placeholder) {
	prefix = p_prefix;
	empty_tooltip = p_empty_tooltip;

	HBoxContainer *add_row = memnew(HBoxContainer);
	add_child(add_row);
	Label *label = memnew(Label(TTR("ID:")));
	add_row->add_child(label);
	id_edit = memnew(LineEdit);
	id_edit->set_h_size_flags(SIZE_EXPAND_FILL);
	id_edit->set_placeholder(p_id_placeholder);
	id_edit->set_accessibility_name(TTR("ID"));
	id_edit->connect(SceneStringName(text_submitted), callable_mp(this, &PlatformNamesSettings::add_pressed).unbind(1));
	add_row->add_child(id_edit);
	Button *add = memnew(Button(TTR("Add")));
	add->connect(SceneStringName(pressed), callable_mp(this, &PlatformNamesSettings::add_pressed));
	add_row->add_child(add);

	MarginContainer *margin = memnew(MarginContainer);
	margin->set_v_size_flags(SIZE_EXPAND_FILL);
	margin->set_theme_type_variation("NoBorderBottomWideWindow");
	add_child(margin);

	tree = memnew(Tree);
	tree->set_hide_root(true);
	tree->set_theme_type_variation("TreeTable");
	tree->set_hide_folding(true);
	tree->set_columns(p_columns.size() + 2);
	tree->set_column_titles_visible(true);
	tree->set_column_title(0, TTR("ID"));
	tree->set_column_title_alignment(0, HORIZONTAL_ALIGNMENT_LEFT);
	tree->set_column_expand(0, true);
	for (int i = 0; i < p_columns.size(); i++) {
		columns.push_back(p_columns[i].first);
		tree->set_column_title(i + 1, p_columns[i].second);
		tree->set_column_title_alignment(i + 1, HORIZONTAL_ALIGNMENT_LEFT);
		tree->set_column_expand(i + 1, true);
	}
	tree->set_column_expand(p_columns.size() + 1, false);
	tree->connect("item_edited", callable_mp(this, &PlatformNamesSettings::item_edited));
	tree->connect("button_clicked", callable_mp(this, &PlatformNamesSettings::button_clicked));
	margin->add_child(tree);
}
