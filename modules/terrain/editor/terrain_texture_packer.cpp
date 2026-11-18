#include "terrain_texture_packer.h"

#include "../terrain_3d_texture_asset.h"
#include "../terrain_3d_util.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "core/io/resource_loader.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"

namespace {

// The image as it is in its file, rather than as imported (compressed, perhaps lossily).
Ref<Image> source_image(const Ref<Texture2D> &p_texture) {
	if (p_texture.is_null()) {
		return Ref<Image>();
	}
	const String path = p_texture->get_path();
	Ref<Image> image;
	if (!path.is_empty() && !path.contains("::")) {
		image.instantiate();
		if (image->load(ProjectSettings::get_singleton()->globalize_path(path)) != OK) {
			image.unref();
		}
	}
	if (image.is_null() || image->is_empty()) {
		image = p_texture->get_image();
		if (image.is_valid()) {
			image = image->duplicate();
			image->decompress();
		}
	}
	if (image.is_valid() && image->has_mipmaps()) {
		image->clear_mipmaps();
	}
	return image;
}

// Saved beside the texture it came from, imported compressed with mipmaps as terrain textures are.
Ref<Texture2D> save(const Ref<Image> &p_image, const String &p_path) {
	if (p_image.is_null() || p_image->save_png(p_path) != OK) {
		ERR_PRINT(vformat("Couldn't write the packed terrain texture %s.", p_path));
		return Ref<Texture2D>();
	}
	const String import_path = p_path + ".import";
	if (!FileAccess::exists(import_path)) {
		Ref<FileAccess> file = FileAccess::open(import_path, FileAccess::WRITE);
		if (file.is_valid()) {
			file->store_string("[remap]\n\nimporter=\"texture\"\ntype=\"CompressedTexture2D\"\n\n[params]\n\ncompress/mode=2\nmipmaps/generate=true\n");
		}
	}
	EditorFileSystem *file_system = EditorFileSystem::get_singleton();
	if (file_system != nullptr) {
		file_system->update_file(p_path);
		file_system->reimport_files({ p_path });
	}
	return ResourceLoader::load(p_path, "Texture2D", ResourceFormatLoader::CACHE_MODE_REPLACE);
}

// Beside the texture, named after it: or, when another texture's packed file has that name (two
// textures with the same colour image but different heights), the next free one.
String packed_path(const Ref<Texture2D> &p_beside, const String &p_suffix, const Ref<Texture2D> &p_own) {
	const String path = p_beside.is_valid() ? p_beside->get_path() : String();
	if (path.is_empty() || path.contains("::")) {
		return String();
	}
	const String own = p_own.is_valid() ? p_own->get_path() : String();
	const String base = path.get_base_dir().path_join(path.get_file().get_basename() + p_suffix);
	for (int n = 1;; n++) {
		const String candidate = base + (n > 1 ? itos(n) : String()) + ".png";
		if (candidate == own || !FileAccess::exists(candidate)) {
			return candidate;
		}
	}
}

} // namespace

void TerrainTexturePacker::pack(Terrain3DTextureAsset *p_asset) {
	if (p_asset == nullptr) {
		return;
	}
	const String sources = p_asset->get_packing_sources();
	const bool albedo_wanted = p_asset->get_height_texture().is_valid() && p_asset->get_albedo_texture().is_valid();
	const bool normal_wanted = p_asset->get_roughness_texture().is_valid();
	const bool up_to_date = sources == p_asset->get_packed_from() &&
			(!albedo_wanted || p_asset->get_packed_albedo_texture().is_valid()) &&
			(!normal_wanted || p_asset->get_packed_normal_texture().is_valid());
	if (up_to_date) {
		return;
	}

	Ref<Texture2D> albedo;
	if (albedo_wanted) {
		const Ref<Image> colour = source_image(p_asset->get_albedo_texture());
		const Ref<Image> height = source_image(p_asset->get_height_texture());
		const String path = packed_path(p_asset->get_albedo_texture(), "_terrain", p_asset->get_packed_albedo_texture());
		if (colour.is_valid() && height.is_valid() && !path.is_empty()) {
			height->resize(colour->get_width(), colour->get_height(), Image::INTERPOLATE_CUBIC);
			albedo = save(Terrain3DUtil::pack_image(colour, height, Ref<Image>()), path);
		}
	}

	Ref<Texture2D> normal;
	if (normal_wanted) {
		Ref<Image> roughness = source_image(p_asset->get_roughness_texture());
		Ref<Image> normals = source_image(p_asset->get_normal_texture());
		const Ref<Texture2D> beside = p_asset->get_normal_texture().is_valid() ? p_asset->get_normal_texture() : p_asset->get_roughness_texture();
		const String path = packed_path(beside, "_terrain", p_asset->get_packed_normal_texture());
		if (roughness.is_valid() && !path.is_empty()) {
			if (normals.is_null()) {
				// No normal map: a flat one.
				normals = Image::create_empty(roughness->get_width(), roughness->get_height(), false, Image::FORMAT_RGB8);
				normals->fill(Color(0.5, 0.5, 1.0));
			}
			roughness->resize(normals->get_width(), normals->get_height(), Image::INTERPOLATE_CUBIC);
			normal = save(Terrain3DUtil::pack_image(normals, roughness, Ref<Image>()), path);
		}
	}

	p_asset->set_packed_from(sources);
	p_asset->set_packed_albedo_texture(albedo);
	p_asset->set_packed_normal_texture(normal);
}
