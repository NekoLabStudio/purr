#include "terrain_import_dialog.h"

#include "../terrain_3d.h"
#include "../terrain_3d_editor.h"
#include "../terrain_3d_util.h"

#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "editor/editor_node.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/gui/editor_spin_slider.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/label.h"

#include <png.h>
#include <csetjmp>
#include <cstring>

namespace {

struct PngBytes {
	const uint8_t *data = nullptr;
	size_t size = 0;
	size_t at = 0;
};

void read_png_bytes(png_structp p_png, png_bytep r_out, png_size_t p_count) {
	PngBytes *bytes = static_cast<PngBytes *>(png_get_io_ptr(p_png));
	if (bytes->at + p_count > bytes->size) {
		png_error(p_png, "The file ends early.");
	}
	memcpy(r_out, bytes->data + bytes->at, p_count);
	bytes->at += p_count;
}

// Godot's PNG loader turns 16-bit images into 8-bit, which would leave a heightmap 256 steps tall,
// terraced: 16-bit PNGs (the usual heightmap) are read here, the first channel at full precision.
// Anything else is left to Godot's loaders (an empty result).
Ref<Image> read_16_bit_png(const String &p_path) {
	const Vector<uint8_t> file = FileAccess::get_file_as_bytes(p_path);
	if (file.size() < 8 || png_sig_cmp(file.ptr(), 0, 8) != 0) {
		return Ref<Image>();
	}
	// Everything libpng's error jump could skip past is made before it.
	PngBytes bytes{ file.ptr(), size_t(file.size()), 0 };
	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	png_infop info = png != nullptr ? png_create_info_struct(png) : nullptr;
	Vector<uint8_t> row;
	Vector<float> heights;
	int width = 0;
	int height = 0;
	if (png == nullptr || info == nullptr || setjmp(png_jmpbuf(png))) {
		png_destroy_read_struct(&png, &info, nullptr);
		return Ref<Image>();
	}
	png_set_read_fn(png, &bytes, read_png_bytes);
	png_read_info(png, info);
	if (png_get_bit_depth(png, info) != 16 || png_get_interlace_type(png, info) != PNG_INTERLACE_NONE) {
		png_destroy_read_struct(&png, &info, nullptr);
		return Ref<Image>();
	}
	width = int(png_get_image_width(png, info));
	height = int(png_get_image_height(png, info));
	const int channels = png_get_channels(png, info);
	row.resize(png_get_rowbytes(png, info));
	heights.resize(width * height);
	float *out = heights.ptrw();
	for (int y = 0; y < height; y++) {
		png_read_row(png, row.ptrw(), nullptr);
		const uint8_t *samples = row.ptr();
		for (int x = 0; x < width; x++) {
			const uint8_t *sample = samples + x * channels * 2; // Big-endian, as PNG stores them.
			*out++ = float((sample[0] << 8) | sample[1]) / 65535.0f;
		}
	}
	png_destroy_read_struct(&png, &info, nullptr);

	Vector<uint8_t> data;
	data.resize(heights.size() * sizeof(float));
	memcpy(data.ptrw(), heights.ptr(), data.size());
	return Image::create_from_data(width, height, false, Image::FORMAT_RF, data);
}

} // namespace

TerrainImportDialog::TerrainImportDialog() {
	set_title(TTR("Import Heightmap"));
	set_ok_button_text(TTR("Import"));
	connect(SceneStringName(confirmed), callable_mp(this, &TerrainImportDialog::_import));

	VBoxContainer *rows = memnew(VBoxContainer);
	add_child(rows);
	file_label = memnew(Label);
	file_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	file_label->set_custom_minimum_size(Size2(320, 0) * EDSCALE);
	rows->add_child(file_label);

	Label *heights = memnew(Label);
	heights->set_text(TTR("The image's darkest and brightest become these heights:"));
	rows->add_child(heights);
	HBoxContainer *range = memnew(HBoxContainer);
	rows->add_child(range);
	lowest = _add_slider(range, TTR("Lowest"), 0.0);
	highest = _add_slider(range, TTR("Highest"), 100.0);

	Label *corner = memnew(Label);
	corner->set_text(TTR("Its top-left corner goes at:"));
	rows->add_child(corner);
	HBoxContainer *position = memnew(HBoxContainer);
	rows->add_child(position);
	corner_x = _add_slider(position, "X", 0.0);
	corner_z = _add_slider(position, "Z", 0.0);

	file_dialog = memnew(EditorFileDialog);
	file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	file_dialog->set_access(EditorFileDialog::ACCESS_FILESYSTEM);
	file_dialog->set_title(TTR("Import Heightmap"));
	file_dialog->add_filter("*.png,*.exr,*.r16,*.raw,*.tga,*.tif,*.webp,*.jpg,*.jpeg", TTR("Heightmaps"));
	file_dialog->connect("file_selected", callable_mp(this, &TerrainImportDialog::_file_chosen));
	add_child(file_dialog);
}

EditorSpinSlider *TerrainImportDialog::_add_slider(Node *p_parent, const String &p_label, double p_value) {
	EditorSpinSlider *slider = memnew(EditorSpinSlider);
	slider->set_label(p_label);
	slider->set_suffix("m");
	slider->set_min(-100000);
	slider->set_max(100000);
	slider->set_step(0.1);
	slider->set_value(p_value);
	slider->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	p_parent->add_child(slider);
	return slider;
}

void TerrainImportDialog::start(Terrain3D *p_terrain, Terrain3DEditor *p_editor) {
	terrain_id = p_terrain->get_instance_id();
	editor = p_editor;
	file_dialog->popup_file_dialog();
}

// Read now, to place it centred on the origin to begin with.
void TerrainImportDialog::_file_chosen(const String &p_path) {
	if (p_path.get_extension().to_lower() == "png") {
		image = read_16_bit_png(p_path);
	}
	if (image.is_null()) {
		image = Terrain3DUtil::load_image(p_path, ResourceFormatLoader::CACHE_MODE_IGNORE, Vector2(0, 1));
	}
	if (image.is_null() || image->is_empty()) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("Couldn't read %s as a heightmap."), p_path.get_file()));
		return;
	}
	const Terrain3D *terrain = Object::cast_to<Terrain3D>(ObjectDB::get_instance(terrain_id));
	const real_t spacing = terrain != nullptr ? terrain->get_vertex_spacing() : 1.0;
	file_label->set_text(vformat(TTR("%s, %d × %d"), p_path.get_file(), image->get_width(), image->get_height()));
	corner_x->set_value(Math::round(-image->get_width() * spacing * 0.5));
	corner_z->set_value(Math::round(-image->get_height() * spacing * 0.5));
	popup_centered();
}

// Values from 0 to 1 become lowest to highest: the data scales each value, then adds the offset.
void TerrainImportDialog::_import() {
	Terrain3D *terrain = Object::cast_to<Terrain3D>(ObjectDB::get_instance(terrain_id));
	if (terrain == nullptr || editor == nullptr || image.is_null()) {
		return;
	}
	const real_t scale = highest->get_value() - lowest->get_value();
	const real_t offset = lowest->get_value();
	editor->set_terrain(terrain);
	editor->import_images(TypedArray<Image>({ image, Variant(), Variant() }), Vector3(corner_x->get_value(), 0, corner_z->get_value()), offset, scale);
	image.unref();
}
