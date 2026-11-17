#include "input_prompt_images.h"

#include "prompt_images.gen.h"

#include "core/config/project_settings.h"
#include "core/io/image.h"
#include "core/io/resource_loader.h"
#include "scene/resources/image_texture.h"

namespace {

// Kenney's vector prompts are drawn on a 64 by 64 canvas.
const float STANDARD_CANVAS = 64.0;

const PromptSvg *find_standard(const String &p_control) {
	for (const PromptSvg &each : PROMPT_STANDARD) {
		if (p_control == each.control) {
			return &each;
		}
	}
	return nullptr;
}

const PromptTiles *find_pixel(const String &p_control) {
	for (const PromptTiles &each : PROMPT_PIXEL) {
		if (p_control == each.control) {
			return &each;
		}
	}
	return nullptr;
}

} // namespace

InputPromptImages::Style InputPromptImages::style() const {
	return String(GLOBAL_GET("input_devices/prompts/style")) == "pixel" ? STYLE_PIXEL : STYLE_STANDARD;
}

Ref<Texture2D> InputPromptImages::standard(const String &p_control, int p_size) {
	const PromptSvg *svg = find_standard(p_control);
	if (!svg) {
		return Ref<Texture2D>();
	}
	Vector<uint8_t> bytes;
	bytes.resize(svg->size);
	memcpy(bytes.ptrw(), svg->data, svg->size);
	Ref<Image> image;
	image.instantiate();
	ERR_FAIL_COND_V(image->load_svg_from_buffer(bytes, p_size / STANDARD_CANVAS) != OK, Ref<Texture2D>());
	return ImageTexture::create_from_image(image);
}

Ref<Texture2D> InputPromptImages::pixel(const String &p_control) {
	const PromptTiles *tiles = find_pixel(p_control);
	if (!tiles) {
		return Ref<Texture2D>();
	}
	if (pixel_sheet.is_null()) {
		Vector<uint8_t> bytes;
		bytes.resize(sizeof(PROMPT_PIXEL_SHEET));
		memcpy(bytes.ptrw(), PROMPT_PIXEL_SHEET, sizeof(PROMPT_PIXEL_SHEET));
		pixel_sheet.instantiate();
		ERR_FAIL_COND_V(pixel_sheet->load_png_from_buffer(bytes) != OK, Ref<Texture2D>());
		pixel_sheet->convert(Image::FORMAT_RGBA8);
	}
	// Wide keys span tiles side by side.
	const int tile = PROMPT_PIXEL_TILE;
	Ref<Image> image = Image::create_empty(tile * tiles->count, tile, false, Image::FORMAT_RGBA8);
	for (int i = 0; i < tiles->count; i++) {
		const int index = tiles->tiles[i];
		const Rect2i from((index % PROMPT_PIXEL_COLUMNS) * tile, (index / PROMPT_PIXEL_COLUMNS) * tile, tile, tile);
		image->blit_rect(pixel_sheet, from, Point2i(i * tile, 0));
	}
	return ImageTexture::create_from_image(image);
}

void InputPromptImages::load_custom() {
	if (custom_loaded) {
		return;
	}
	custom_loaded = true;
	const String path = GLOBAL_GET("input_devices/prompts/custom_set");
	if (!path.is_empty()) {
		custom = ResourceLoader::load(path);
		ERR_FAIL_COND_MSG(custom.is_null(), vformat("input_devices/prompts/custom_set (%s) isn't an InputPromptSet.", path));
	}
}

Ref<Texture2D> InputPromptImages::get(const String &p_control, int p_size) {
	load_custom();
	if (custom.is_valid()) {
		const Ref<Texture2D> own = custom->get_image(p_control);
		if (own.is_valid()) {
			return own;
		}
	}
	const bool pixel_style = style() == STYLE_PIXEL;
	const String key = pixel_style ? "pixel:" + p_control : vformat("%d:%s", p_size, p_control);
	const Ref<Texture2D> *known = made.getptr(key);
	if (known) {
		return *known;
	}
	const Ref<Texture2D> texture = pixel_style ? pixel(p_control) : standard(p_control, MAX(p_size, 1));
	made.insert(key, texture);
	return texture;
}

bool InputPromptImages::has(const String &p_control) {
	load_custom();
	if (custom.is_valid() && custom->get_image(p_control).is_valid()) {
		return true;
	}
	return style() == STYLE_PIXEL ? find_pixel(p_control) != nullptr : find_standard(p_control) != nullptr;
}
