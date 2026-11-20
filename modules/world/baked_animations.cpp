#include "baked_animations.h"

#include "core/io/image.h"
#include "core/object/class_db.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/animation/animation_player.h"
#include "scene/resources/image_texture.h"

namespace {
constexpr int MAX_TEXTURE_WIDTH = 2048;

MeshInstance3D *find_skinned_mesh(Node *p_node) {
	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_node);
	if (mesh_instance != nullptr && mesh_instance->get_mesh().is_valid() && Object::cast_to<Skeleton3D>(mesh_instance->get_node_or_null(mesh_instance->get_skeleton_path())) != nullptr) {
		return mesh_instance;
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		MeshInstance3D *found = find_skinned_mesh(p_node->get_child(i));
		if (found != nullptr) {
			return found;
		}
	}
	return nullptr;
}
} // namespace

Error BakedAnimations::bake(Node *p_character, AnimationPlayer *p_player, const PackedStringArray &p_animations, float p_frames_per_second) {
	ERR_FAIL_NULL_V(p_character, ERR_INVALID_PARAMETER);
	ERR_FAIL_NULL_V(p_player, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V_MSG(!p_character->is_inside_tree(), ERR_UNCONFIGURED, "The character must be in the scene tree to be baked.");
	ERR_FAIL_COND_V(p_frames_per_second <= 0.0, ERR_INVALID_PARAMETER);
	MeshInstance3D *source = find_skinned_mesh(p_character);
	ERR_FAIL_NULL_V_MSG(source, ERR_DOES_NOT_EXIST, "The character has no MeshInstance3D with a Skeleton3D.");
	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(source->get_node(source->get_skeleton_path()));
	const Node3D *character_3d = Object::cast_to<Node3D>(p_character);
	// Baked in the character's own space, so a copy is drawn as the character stood.
	const Transform3D to_character = (character_3d != nullptr ? character_3d->get_global_transform().affine_inverse() : Transform3D()) * source->get_global_transform();
	const Basis normal_basis = to_character.basis.inverse().transposed();

	PackedStringArray names = p_animations;
	if (names.is_empty()) {
		LocalVector<StringName> list;
		p_player->get_animation_list(&list);
		for (const StringName &name : list) {
			names.push_back(name);
		}
	}
	ERR_FAIL_COND_V_MSG(names.is_empty(), ERR_DOES_NOT_EXIST, "There are no animations to bake.");

	// The character at rest, as one surface: what's drawn, its vertices moved by the textures.
	const Ref<Mesh> rest = source->get_mesh();
	PackedVector3Array rest_vertices;
	PackedVector3Array rest_normals;
	PackedVector2Array rest_uvs;
	PackedInt32Array rest_indices;
	// Each vertex's skinning: up to 8 binds and their weights, as the mesh has them.
	LocalVector<int> vertex_binds;
	LocalVector<float> vertex_weights;
	LocalVector<Vector3> source_vertices;
	LocalVector<Vector3> source_normals;
	constexpr int MAX_INFLUENCES = 8;
	for (int s = 0; s < rest->get_surface_count(); s++) {
		const Array arrays = rest->surface_get_arrays(s);
		const PackedInt32Array bones = arrays[Mesh::ARRAY_BONES];
		const PackedFloat32Array weights = arrays[Mesh::ARRAY_WEIGHTS];
		const PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
		const PackedVector3Array surface_normals = arrays[Mesh::ARRAY_NORMAL];
		const PackedVector2Array uvs = arrays[Mesh::ARRAY_TEX_UV];
		const PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
		const int first = rest_vertices.size();
		const int influences = vertices.is_empty() ? 0 : MIN(bones.size() / vertices.size(), MAX_INFLUENCES);
		for (int v = 0; v < vertices.size(); v++) {
			for (int k = 0; k < MAX_INFLUENCES; k++) {
				const bool used = k < influences && weights.size() == bones.size();
				vertex_binds.push_back(used ? bones[v * (bones.size() / vertices.size()) + k] : 0);
				vertex_weights.push_back(used ? weights[v * (bones.size() / vertices.size()) + k] : (k == 0 && influences == 0 ? 1.0f : 0.0f));
			}
			source_vertices.push_back(vertices[v]);
			source_normals.push_back(surface_normals.size() == vertices.size() ? surface_normals[v] : Vector3(0, 1, 0));
			rest_vertices.push_back(to_character.xform(vertices[v]));
			rest_normals.push_back(surface_normals.size() == vertices.size() ? normal_basis.xform(surface_normals[v]).normalized() : Vector3(0, 1, 0));
			rest_uvs.push_back(uvs.size() == vertices.size() ? uvs[v] : Vector2());
		}
		if (indices.is_empty()) {
			for (int v = 0; v < vertices.size(); v++) {
				rest_indices.push_back(first + v);
			}
		} else {
			for (int i = 0; i < indices.size(); i++) {
				rest_indices.push_back(first + indices[i]);
			}
		}
	}
	const int vertex_count = rest_vertices.size();
	ERR_FAIL_COND_V(vertex_count == 0, ERR_INVALID_DATA);
	texture_width = MIN(vertex_count, MAX_TEXTURE_WIDTH);
	rows_per_frame = (vertex_count + texture_width - 1) / texture_width;

	// The mesh's bone numbers are its skin's binds: each a bone and the bone's rest inverted. A mesh
	// without a skin of its own binds each number to the bone of that number.
	const Ref<Skin> skin = source->get_skin();
	LocalVector<int> bind_bones;
	LocalVector<Transform3D> bind_poses;
	const int bind_count = skin.is_valid() ? skin->get_bind_count() : skeleton->get_bone_count();
	for (int b = 0; b < bind_count; b++) {
		int bone = b;
		Transform3D pose = skeleton->get_bone_global_rest(b).affine_inverse();
		if (skin.is_valid()) {
			bone = skin->get_bind_name(b) != StringName() ? skeleton->find_bone(skin->get_bind_name(b)) : skin->get_bind_bone(b);
			pose = skin->get_bind_pose(b);
		}
		bind_bones.push_back(bone);
		bind_poses.push_back(pose);
	}
	LocalVector<Transform3D> bind_transforms;
	LocalVector<Basis> bind_normal_bases;
	bind_transforms.resize(bind_count);
	bind_normal_bases.resize(bind_count);

	// Every frame of every animation, one after another.
	LocalVector<float> position_data;
	LocalVector<float> normal_data;
	Dictionary baked;
	int frame = 0;
	AABB bounds(rest_vertices[0], Vector3());
	for (const String &name : names) {
		const Ref<Animation> animation = p_player->get_animation(name);
		ERR_CONTINUE_MSG(animation.is_null(), vformat("There's no animation \"%s\" to bake.", name));
		const bool loops = animation->get_loop_mode() != Animation::LOOP_NONE;
		const double length = animation->get_length();
		// A looping animation's last frame is its first again; one that doesn't loop ends on its last.
		const int frames = MAX(1, int(Math::round(length * p_frames_per_second)) + (loops ? 0 : 1));
		p_player->play(name);
		for (int f = 0; f < frames; f++) {
			p_player->seek(MIN(f / double(p_frames_per_second), length), true);
			skeleton->force_update_all_bone_transforms();
			for (int b = 0; b < bind_count; b++) {
				const int bone = bind_bones[b];
				// As Godot draws a skinned mesh: bones in the skeleton's space, then the mesh's own place.
				bind_transforms[b] = bone >= 0 ? to_character * skeleton->get_bone_global_pose(bone) * bind_poses[b] : to_character;
				bind_normal_bases[b] = bind_transforms[b].basis.inverse().transposed();
			}
			const int offset = position_data.size();
			position_data.resize(offset + texture_width * rows_per_frame * 4);
			normal_data.resize(offset + texture_width * rows_per_frame * 4);
			for (int v = 0; v < vertex_count; v++) {
				Vector3 at;
				Vector3 facing;
				for (int k = 0; k < MAX_INFLUENCES; k++) {
					const float weight = vertex_weights[v * MAX_INFLUENCES + k];
					const int bind = vertex_binds[v * MAX_INFLUENCES + k];
					if (weight <= 0.0f || bind < 0 || bind >= bind_count) {
						continue;
					}
					at += bind_transforms[bind].xform(source_vertices[v]) * weight;
					facing += bind_normal_bases[bind].xform(source_normals[v]) * weight;
				}
				facing = facing.length_squared() > 0.0f ? facing.normalized() : Vector3(0, 1, 0);
				bounds.expand_to(at);
				float *p = &position_data[offset + v * 4];
				p[0] = at.x;
				p[1] = at.y;
				p[2] = at.z;
				p[3] = 1.0;
				float *n = &normal_data[offset + v * 4];
				n[0] = facing.x;
				n[1] = facing.y;
				n[2] = facing.z;
				n[3] = 0.0;
			}
		}
		baked[StringName(name)] = Vector4(frame, frames, p_frames_per_second, loops ? 1.0 : 0.0);
		frame += frames;
	}
	p_player->stop();
	ERR_FAIL_COND_V(frame == 0, ERR_DOES_NOT_EXIST);
	animations = baked;

	const int height = frame * rows_per_frame;
	ERR_FAIL_COND_V_MSG(height > Image::MAX_HEIGHT || height > 16384, ERR_OUT_OF_MEMORY, vformat("%d frames of a %d-vertex character need %d rows of texture, past the 16384 GPUs take: bake fewer animations or frames a second, or use a simpler mesh.", frame, vertex_count, height));
	PackedByteArray bytes;
	bytes.resize(position_data.size() * sizeof(float));
	memcpy(bytes.ptrw(), position_data.ptr(), bytes.size());
	Ref<Image> image = Image::create_from_data(texture_width, height, false, Image::FORMAT_RGBAF, bytes);
	image->convert(Image::FORMAT_RGBAH);
	positions = ImageTexture::create_from_image(image);
	memcpy(bytes.ptrw(), normal_data.ptr(), bytes.size());
	image = Image::create_from_data(texture_width, height, false, Image::FORMAT_RGBAF, bytes);
	image->convert(Image::FORMAT_RGBAH);
	normals = ImageTexture::create_from_image(image);

	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = rest_vertices;
	arrays[Mesh::ARRAY_NORMAL] = rest_normals;
	arrays[Mesh::ARRAY_TEX_UV] = rest_uvs;
	arrays[Mesh::ARRAY_INDEX] = rest_indices;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	if (rest->get_surface_count() > 0) {
		mesh->surface_set_material(0, rest->surface_get_material(0));
	}
	// Culled by everywhere any frame reaches, not where the character stood at rest.
	mesh->set_custom_aabb(bounds.grow(0.01));
	emit_changed();
	return OK;
}

Color BakedAnimations::get_custom_data(const StringName &p_animation, float p_time) const {
	ERR_FAIL_COND_V_MSG(!animations.has(p_animation), Color(), vformat("There's no baked animation \"%s\".", p_animation));
	const Vector4 animation = animations[p_animation];
	const float frames = animation.y;
	float at = p_time * animation.z;
	at = animation.w > 0.5 ? Math::fposmod(at, frames) : CLAMP(at, 0.0f, frames - 1.0f);
	// Read by BakedAnimationMaterial: the first frame, how many, where in them, and whether it loops.
	return Color(animation.x, frames, at, animation.w);
}

float BakedAnimations::get_length(const StringName &p_animation) const {
	ERR_FAIL_COND_V(!animations.has(p_animation), 0.0);
	const Vector4 animation = animations[p_animation];
	return (animation.w > 0.5 ? animation.y : animation.y - 1.0) / animation.z;
}

PackedStringArray BakedAnimations::get_animation_names() const {
	PackedStringArray names;
	for (const KeyValue<Variant, Variant> &entry : animations) {
		names.push_back(entry.key);
	}
	return names;
}

void BakedAnimations::set_mesh(const Ref<ArrayMesh> &p_mesh) {
	mesh = p_mesh;
	emit_changed();
}

Ref<ArrayMesh> BakedAnimations::get_mesh() const {
	return mesh;
}

void BakedAnimations::set_positions(const Ref<Texture2D> &p_texture) {
	positions = p_texture;
	emit_changed();
}

Ref<Texture2D> BakedAnimations::get_positions() const {
	return positions;
}

void BakedAnimations::set_normals(const Ref<Texture2D> &p_texture) {
	normals = p_texture;
	emit_changed();
}

Ref<Texture2D> BakedAnimations::get_normals() const {
	return normals;
}

void BakedAnimations::set_texture_width(int p_width) {
	texture_width = p_width;
	emit_changed();
}

int BakedAnimations::get_texture_width() const {
	return texture_width;
}

void BakedAnimations::set_rows_per_frame(int p_rows) {
	rows_per_frame = p_rows;
	emit_changed();
}

int BakedAnimations::get_rows_per_frame() const {
	return rows_per_frame;
}

void BakedAnimations::set_animations(const Dictionary &p_animations) {
	animations = p_animations;
	emit_changed();
}

Dictionary BakedAnimations::get_animations() const {
	return animations;
}

void BakedAnimations::_bind_methods() {
	ClassDB::bind_method(D_METHOD("bake", "character", "player", "animations", "frames_per_second"), &BakedAnimations::bake, DEFVAL(PackedStringArray()), DEFVAL(30.0));
	ClassDB::bind_method(D_METHOD("get_custom_data", "animation", "time"), &BakedAnimations::get_custom_data);
	ClassDB::bind_method(D_METHOD("get_length", "animation"), &BakedAnimations::get_length);
	ClassDB::bind_method(D_METHOD("get_animation_names"), &BakedAnimations::get_animation_names);
	ClassDB::bind_method(D_METHOD("set_mesh", "mesh"), &BakedAnimations::set_mesh);
	ClassDB::bind_method(D_METHOD("get_mesh"), &BakedAnimations::get_mesh);
	ClassDB::bind_method(D_METHOD("set_positions", "texture"), &BakedAnimations::set_positions);
	ClassDB::bind_method(D_METHOD("get_positions"), &BakedAnimations::get_positions);
	ClassDB::bind_method(D_METHOD("set_normals", "texture"), &BakedAnimations::set_normals);
	ClassDB::bind_method(D_METHOD("get_normals"), &BakedAnimations::get_normals);
	ClassDB::bind_method(D_METHOD("set_texture_width", "width"), &BakedAnimations::set_texture_width);
	ClassDB::bind_method(D_METHOD("get_texture_width"), &BakedAnimations::get_texture_width);
	ClassDB::bind_method(D_METHOD("set_rows_per_frame", "rows"), &BakedAnimations::set_rows_per_frame);
	ClassDB::bind_method(D_METHOD("get_rows_per_frame"), &BakedAnimations::get_rows_per_frame);
	ClassDB::bind_method(D_METHOD("set_animations", "animations"), &BakedAnimations::set_animations);
	ClassDB::bind_method(D_METHOD("get_animations"), &BakedAnimations::get_animations);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh", PROPERTY_HINT_RESOURCE_TYPE, "ArrayMesh"), "set_mesh", "get_mesh");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "positions", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_positions", "get_positions");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "normals", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"), "set_normals", "get_normals");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "texture_width"), "set_texture_width", "get_texture_width");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "rows_per_frame"), "set_rows_per_frame", "get_rows_per_frame");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "animations"), "set_animations", "get_animations");
}
