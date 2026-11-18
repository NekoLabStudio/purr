#pragma once

// Importing a heightmap: a file dialog for the image, then a dialog for the heights its values
// span and where its corner goes. The import is one step to undo.

#include "scene/gui/dialogs.h"

class EditorFileDialog;
class EditorSpinSlider;
class Image;
class Label;
class Terrain3D;
class Terrain3DEditor;

class TerrainImportDialog : public ConfirmationDialog {
	GDCLASS(TerrainImportDialog, ConfirmationDialog);

	ObjectID terrain_id;
	Terrain3DEditor *editor = nullptr;
	Ref<Image> image;

	EditorFileDialog *file_dialog = nullptr;
	Label *file_label = nullptr;
	EditorSpinSlider *lowest = nullptr;
	EditorSpinSlider *highest = nullptr;
	EditorSpinSlider *corner_x = nullptr;
	EditorSpinSlider *corner_z = nullptr;

	EditorSpinSlider *_add_slider(Node *p_parent, const String &p_label, double p_value);
	void _file_chosen(const String &p_path);
	void _import();

public:
	void start(Terrain3D *p_terrain, Terrain3DEditor *p_editor);

	TerrainImportDialog();
};
