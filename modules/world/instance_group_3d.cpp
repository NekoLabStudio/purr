#include "instance_group_3d.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/rendering/rendering_server.h"

namespace {
constexpr int TRANSFORM_FLOATS = 12;

// Room for a chunk's copies, in steps that double, so copies crossing between chunks don't
// reallocate a buffer every frame.
int capacity_for(int p_count) {
	int capacity = 16;
	while (capacity < p_count) {
		capacity *= 2;
	}
	return capacity;
}
} // namespace

InstanceGroup3D::~InstanceGroup3D() {
	_free_chunks();
}

int InstanceGroup3D::_stride() const {
	return TRANSFORM_FLOATS + (use_colors ? 4 : 0) + (use_custom_data ? 4 : 0);
}

void InstanceGroup3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_WORLD:
			set_notify_transform(true);
			_queue_update();
			break;
		case NOTIFICATION_EXIT_WORLD:
			_free_chunks();
			break;
		case NOTIFICATION_TRANSFORM_CHANGED: {
			const Transform3D transform = get_global_transform();
			for (const KeyValue<Vector3i, Chunk> &chunk : chunks) {
				RS::get_singleton()->instance_set_transform(chunk.value.instance, transform);
			}
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			const bool visible = is_visible_in_tree();
			for (const KeyValue<Vector3i, Chunk> &chunk : chunks) {
				RS::get_singleton()->instance_set_visible(chunk.value.instance, visible);
			}
		} break;
	}
}

void InstanceGroup3D::_free_chunks() {
	RenderingServer *rs = RS::get_singleton();
	if (rs == nullptr) {
		return;
	}
	for (const KeyValue<Vector3i, Chunk> &chunk : chunks) {
		rs->free_rid(chunk.value.instance);
		rs->free_rid(chunk.value.multimesh);
	}
	chunks.clear();
}

// Whatever changes in a frame is drawn together once the frame's scripts have run.
void InstanceGroup3D::_queue_update() {
	if (update_queued || !is_inside_world()) {
		return;
	}
	update_queued = true;
	callable_mp(this, &InstanceGroup3D::_update).call_deferred();
}

void InstanceGroup3D::_apply_settings(const Chunk &p_chunk) const {
	RenderingServer *rs = RS::get_singleton();
	rs->multimesh_set_mesh(p_chunk.multimesh, mesh.is_valid() ? mesh->get_rid() : RID());
	rs->instance_geometry_set_material_override(p_chunk.instance, material_override.is_valid() ? material_override->get_rid() : RID());
	rs->instance_geometry_set_cast_shadows_setting(p_chunk.instance, RenderingServerEnums::ShadowCastingSetting(cast_shadow));
	rs->instance_set_layer_mask(p_chunk.instance, layers);
	rs->instance_geometry_set_visibility_range(p_chunk.instance, 0.0, visibility_range_end, 0.0, 0.0, RenderingServerEnums::VISIBILITY_RANGE_FADE_DISABLED);
}

void InstanceGroup3D::_update() {
	update_queued = false;
	if (!is_inside_world()) {
		return;
	}
	RenderingServer *rs = RS::get_singleton();
	const int count = get_count();
	const float *source = transforms.ptr();

	// Which copies each chunk has, by the cell their origin is in.
	HashMap<Vector3i, LocalVector<int>> members;
	for (int i = 0; i < count; i++) {
		const float *t = source + i * TRANSFORM_FLOATS;
		const Vector3i cell = Vector3i((Vector3(t[3], t[7], t[11]) / chunk_size).floor());
		members[cell].push_back(i);
	}

	// Chunks nothing is in now go.
	LocalVector<Vector3i> emptied;
	for (const KeyValue<Vector3i, Chunk> &chunk : chunks) {
		if (!members.has(chunk.key)) {
			emptied.push_back(chunk.key);
		}
	}
	for (const Vector3i &cell : emptied) {
		rs->free_rid(chunks[cell].instance);
		rs->free_rid(chunks[cell].multimesh);
		chunks.erase(cell);
	}

	const int stride = _stride();
	const Transform3D transform = get_global_transform();
	const RID scenario = get_world_3d()->get_scenario();
	PackedFloat32Array buffer;
	for (const KeyValue<Vector3i, LocalVector<int>> &cell : members) {
		Chunk *chunk = chunks.getptr(cell.key);
		if (chunk == nullptr) {
			Chunk made;
			made.multimesh = rs->multimesh_create();
			made.instance = rs->instance_create();
			rs->instance_set_base(made.instance, made.multimesh);
			// So the chunk is this node's: clicked in the editor, or found by a query of what's where.
			rs->instance_attach_object_instance_id(made.instance, get_instance_id());
			rs->instance_set_scenario(made.instance, scenario);
			rs->instance_set_transform(made.instance, transform);
			rs->instance_set_visible(made.instance, is_visible_in_tree());
			_apply_settings(made);
			chunk = &chunks.insert(cell.key, made)->value;
		}
		const int in_chunk = cell.value.size();
		if (chunk->capacity < in_chunk || chunk->capacity > capacity_for(in_chunk) * 4) {
			chunk->capacity = capacity_for(in_chunk);
			rs->multimesh_allocate_data(chunk->multimesh, chunk->capacity, RenderingServerEnums::MULTIMESH_TRANSFORM_3D, use_colors, use_custom_data);
		}
		buffer.resize(chunk->capacity * stride);
		float *write = buffer.ptrw();
		for (int i = 0; i < in_chunk; i++) {
			const int copy = cell.value[i];
			float *to = write + i * stride;
			memcpy(to, source + copy * TRANSFORM_FLOATS, sizeof(float) * TRANSFORM_FLOATS);
			int at = TRANSFORM_FLOATS;
			if (use_colors) {
				const Color colour = copy < colors.size() ? colors[copy] : Color(1, 1, 1, 1);
				to[at++] = colour.r;
				to[at++] = colour.g;
				to[at++] = colour.b;
				to[at++] = colour.a;
			}
			if (use_custom_data) {
				const Color data = copy < custom_data.size() ? custom_data[copy] : Color(0, 0, 0, 0);
				to[at++] = data.r;
				to[at++] = data.g;
				to[at++] = data.b;
				to[at++] = data.a;
			}
		}
		// Room beyond the copies isn't drawn, but counts towards the chunk's box: it repeats the first
		// copy, so the box is the copies' own (zeros would stretch it back to the node's origin).
		for (int i = in_chunk; i < chunk->capacity; i++) {
			memcpy(write + i * stride, write, sizeof(float) * stride);
		}
		rs->multimesh_set_buffer(chunk->multimesh, buffer);
		rs->multimesh_set_visible_instances(chunk->multimesh, in_chunk);
	}
}

void InstanceGroup3D::set_mesh(const Ref<Mesh> &p_mesh) {
	mesh = p_mesh;
	for (const KeyValue<Vector3i, Chunk> &chunk : chunks) {
		_apply_settings(chunk.value);
	}
}

Ref<Mesh> InstanceGroup3D::get_mesh() const {
	return mesh;
}

void InstanceGroup3D::set_material_override(const Ref<Material> &p_material) {
	material_override = p_material;
	for (const KeyValue<Vector3i, Chunk> &chunk : chunks) {
		_apply_settings(chunk.value);
	}
}

Ref<Material> InstanceGroup3D::get_material_override() const {
	return material_override;
}

void InstanceGroup3D::set_cast_shadows_setting(GeometryInstance3D::ShadowCastingSetting p_setting) {
	cast_shadow = p_setting;
	for (const KeyValue<Vector3i, Chunk> &chunk : chunks) {
		_apply_settings(chunk.value);
	}
}

GeometryInstance3D::ShadowCastingSetting InstanceGroup3D::get_cast_shadows_setting() const {
	return cast_shadow;
}

void InstanceGroup3D::set_layer_mask(uint32_t p_mask) {
	layers = p_mask;
	for (const KeyValue<Vector3i, Chunk> &chunk : chunks) {
		_apply_settings(chunk.value);
	}
}

uint32_t InstanceGroup3D::get_layer_mask() const {
	return layers;
}

void InstanceGroup3D::set_visibility_range_end(float p_distance) {
	visibility_range_end = MAX(p_distance, 0.0f);
	for (const KeyValue<Vector3i, Chunk> &chunk : chunks) {
		_apply_settings(chunk.value);
	}
}

float InstanceGroup3D::get_visibility_range_end() const {
	return visibility_range_end;
}

void InstanceGroup3D::set_chunk_size(float p_size) {
	chunk_size = MAX(p_size, 1.0f);
	_free_chunks();
	_queue_update();
}

float InstanceGroup3D::get_chunk_size() const {
	return chunk_size;
}

// A chunk's buffer has room for colours and custom data only if it was made with it.
void InstanceGroup3D::set_use_colors(bool p_use) {
	use_colors = p_use;
	_free_chunks();
	_queue_update();
}

bool InstanceGroup3D::is_using_colors() const {
	return use_colors;
}

void InstanceGroup3D::set_use_custom_data(bool p_use) {
	use_custom_data = p_use;
	_free_chunks();
	_queue_update();
}

bool InstanceGroup3D::is_using_custom_data() const {
	return use_custom_data;
}

void InstanceGroup3D::set_transforms(const PackedFloat32Array &p_transforms) {
	ERR_FAIL_COND_MSG(p_transforms.size() % TRANSFORM_FLOATS != 0, "InstanceGroup3D.set_transforms() takes 12 floats for each copy.");
	transforms = p_transforms;
	_queue_update();
}

PackedFloat32Array InstanceGroup3D::get_transforms() const {
	return transforms;
}

void InstanceGroup3D::set_colors(const PackedColorArray &p_colors) {
	colors = p_colors;
	_queue_update();
}

PackedColorArray InstanceGroup3D::get_colors() const {
	return colors;
}

void InstanceGroup3D::set_custom_data(const PackedColorArray &p_custom_data) {
	custom_data = p_custom_data;
	_queue_update();
}

PackedColorArray InstanceGroup3D::get_custom_data() const {
	return custom_data;
}

int InstanceGroup3D::get_count() const {
	return transforms.size() / TRANSFORM_FLOATS;
}

int InstanceGroup3D::get_chunk_count() const {
	return chunks.size();
}

void InstanceGroup3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mesh", "mesh"), &InstanceGroup3D::set_mesh);
	ClassDB::bind_method(D_METHOD("get_mesh"), &InstanceGroup3D::get_mesh);
	ClassDB::bind_method(D_METHOD("set_material_override", "material"), &InstanceGroup3D::set_material_override);
	ClassDB::bind_method(D_METHOD("get_material_override"), &InstanceGroup3D::get_material_override);
	ClassDB::bind_method(D_METHOD("set_cast_shadows_setting", "shadow_casting_setting"), &InstanceGroup3D::set_cast_shadows_setting);
	ClassDB::bind_method(D_METHOD("get_cast_shadows_setting"), &InstanceGroup3D::get_cast_shadows_setting);
	ClassDB::bind_method(D_METHOD("set_layer_mask", "mask"), &InstanceGroup3D::set_layer_mask);
	ClassDB::bind_method(D_METHOD("get_layer_mask"), &InstanceGroup3D::get_layer_mask);
	ClassDB::bind_method(D_METHOD("set_visibility_range_end", "distance"), &InstanceGroup3D::set_visibility_range_end);
	ClassDB::bind_method(D_METHOD("get_visibility_range_end"), &InstanceGroup3D::get_visibility_range_end);
	ClassDB::bind_method(D_METHOD("set_chunk_size", "size"), &InstanceGroup3D::set_chunk_size);
	ClassDB::bind_method(D_METHOD("get_chunk_size"), &InstanceGroup3D::get_chunk_size);
	ClassDB::bind_method(D_METHOD("set_use_colors", "enable"), &InstanceGroup3D::set_use_colors);
	ClassDB::bind_method(D_METHOD("is_using_colors"), &InstanceGroup3D::is_using_colors);
	ClassDB::bind_method(D_METHOD("set_use_custom_data", "enable"), &InstanceGroup3D::set_use_custom_data);
	ClassDB::bind_method(D_METHOD("is_using_custom_data"), &InstanceGroup3D::is_using_custom_data);
	ClassDB::bind_method(D_METHOD("set_transforms", "transforms"), &InstanceGroup3D::set_transforms);
	ClassDB::bind_method(D_METHOD("get_transforms"), &InstanceGroup3D::get_transforms);
	ClassDB::bind_method(D_METHOD("set_colors", "colors"), &InstanceGroup3D::set_colors);
	ClassDB::bind_method(D_METHOD("get_colors"), &InstanceGroup3D::get_colors);
	ClassDB::bind_method(D_METHOD("set_custom_data", "custom_data"), &InstanceGroup3D::set_custom_data);
	ClassDB::bind_method(D_METHOD("get_custom_data"), &InstanceGroup3D::get_custom_data);
	ClassDB::bind_method(D_METHOD("get_count"), &InstanceGroup3D::get_count);
	ClassDB::bind_method(D_METHOD("get_chunk_count"), &InstanceGroup3D::get_chunk_count);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"), "set_mesh", "get_mesh");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material_override", PROPERTY_HINT_RESOURCE_TYPE, "BaseMaterial3D,ShaderMaterial"), "set_material_override", "get_material_override");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cast_shadow", PROPERTY_HINT_ENUM, "Off,On,Double-Sided,Shadows Only"), "set_cast_shadows_setting", "get_cast_shadows_setting");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "layers", PROPERTY_HINT_LAYERS_3D_RENDER), "set_layer_mask", "get_layer_mask");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "visibility_range_end", PROPERTY_HINT_RANGE, "0,4096,0.01,or_greater,suffix:m"), "set_visibility_range_end", "get_visibility_range_end");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "chunk_size", PROPERTY_HINT_RANGE, "1,1024,0.1,or_greater,suffix:m"), "set_chunk_size", "get_chunk_size");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_colors"), "set_use_colors", "is_using_colors");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_custom_data"), "set_use_custom_data", "is_using_custom_data");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "transforms", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), "set_transforms", "get_transforms");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_COLOR_ARRAY, "colors", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), "set_colors", "get_colors");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_COLOR_ARRAY, "custom_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR), "set_custom_data", "get_custom_data");
}
