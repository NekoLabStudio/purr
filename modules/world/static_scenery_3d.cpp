#include "static_scenery_3d.h"

#include "core/config/engine.h"
#include "core/io/marshalls.h"
#include "core/object/class_db.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/multimesh_instance_3d.h"
#include "scene/resources/3d/importer_mesh.h"
#include "scene/resources/material.h"
#include "scene/resources/multimesh.h"
#include "scene/resources/surface_tool.h"
#include "servers/rendering/rendering_server.h"

#include <algorithm>

namespace {
constexpr float OCCLUDER_MIN_SIZE = 2.0; // A piece occludes when its two largest sides are this long.
constexpr float OCCLUDER_SIMPLIFICATION = 0.1; // Metres an occluder's simplified surface may stray.
constexpr float LOD_NORMAL_MERGE_ANGLE = 60.0; // As Godot's scene importer has it.
// A MultiMesh is merged only while its copies come to this many vertices: past that (grass, a
// forest), drawing it instanced is cheaper than one huge mesh.
constexpr int MULTIMESH_MERGE_VERTICES = 65536;

bool is_transparent(const Ref<Material> &p_material) {
	const Ref<BaseMaterial3D> base = p_material;
	return base.is_valid() && base->get_transparency() != BaseMaterial3D::TRANSPARENCY_DISABLED;
}

// Shown as far as the scenery is concerned: the scenery itself may be hidden, and its merged
// meshes with it.
bool _shown_under_scenery(const Node3D *p_node) {
	const Node *at = p_node;
	while (at != nullptr && Object::cast_to<StaticScenery3D>(at) == nullptr) {
		const Node3D *node_3d = Object::cast_to<Node3D>(at);
		if (node_3d != nullptr && !node_3d->is_visible()) {
			return false;
		}
		at = at->get_parent();
	}
	return true;
}

// A material's per-instance parameters would all take the merged mesh's (default) values.
bool _has_instance_parameters(GeometryInstance3D *p_instance) {
	List<PropertyInfo> parameters;
	RS::get_singleton()->instance_geometry_get_shader_parameter_list(p_instance->get_instance(), &parameters);
	return !parameters.is_empty();
}

bool occludes(const AABB &p_aabb) {
	real_t sides[3] = { p_aabb.size.x, p_aabb.size.y, p_aabb.size.z };
	std::sort(sides, sides + 3);
	return sides[1] >= OCCLUDER_MIN_SIZE;
}
} // namespace

StaticScenery3D::~StaticScenery3D() {
	if (task != WorkerThreadPool::INVALID_TASK_ID) {
		WorkerThreadPool::get_singleton()->wait_for_task_completion(task);
		task = WorkerThreadPool::INVALID_TASK_ID;
	}
	if (job != nullptr) {
		memdelete(job);
		job = nullptr;
	}
	_clear();
}

void StaticScenery3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (!Engine::get_singleton()->is_editor_hint()) {
				merge();
			}
		} break;
		case NOTIFICATION_ENTER_TREE: {
			// Taken out of the tree and put back: merged again from what's there now.
			if (is_ready() && !Engine::get_singleton()->is_editor_hint()) {
				merge();
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (task != WorkerThreadPool::INVALID_TASK_ID) {
				WorkerThreadPool::get_singleton()->wait_for_task_completion(task);
				task = WorkerThreadPool::INVALID_TASK_ID;
				memdelete(job);
				job = nullptr;
				set_process_internal(false);
			}
			_clear();
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_check_job();
		} break;
		case NOTIFICATION_TRANSFORM_CHANGED: {
			const Transform3D transform = get_global_transform();
			for (const Drawn &each : drawn) {
				RS::get_singleton()->instance_set_transform(each.instance, transform);
				if (each.occluder_instance.is_valid()) {
					RS::get_singleton()->instance_set_transform(each.occluder_instance, transform);
				}
			}
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			_show(is_visible_in_tree());
		} break;
	}
}

void StaticScenery3D::_show(bool p_merged_visible) {
	for (const Drawn &each : drawn) {
		RS::get_singleton()->instance_set_visible(each.instance, p_merged_visible);
		if (each.occluder_instance.is_valid()) {
			RS::get_singleton()->instance_set_visible(each.occluder_instance, p_merged_visible);
		}
	}
}

// The merged meshes go and the originals draw again, as they were.
void StaticScenery3D::_clear() {
	RenderingServer *rs = RS::get_singleton();
	if (rs != nullptr) {
		for (const Drawn &each : drawn) {
			rs->free_rid(each.instance);
			if (each.occluder_instance.is_valid()) {
				rs->free_rid(each.occluder_instance);
				rs->free_rid(each.occluder);
			}
		}
		for (const ObjectID &id : hidden) {
			GeometryInstance3D *original = ObjectDB::get_instance<GeometryInstance3D>(id);
			if (original != nullptr) {
				rs->instance_set_visible(original->get_instance(), original->is_visible_in_tree());
			}
		}
	}
	drawn.clear();
	hidden.clear();
	if (merged) {
		merged = false;
		set_notify_transform(false);
	}
}

void StaticScenery3D::merge() {
	ERR_FAIL_COND_MSG(!is_inside_tree(), "StaticScenery3D can only merge what's under it in the tree.");
	if (task != WorkerThreadPool::INVALID_TASK_ID) {
		// A merge already under way: what it made is replaced by this one's.
		WorkerThreadPool::get_singleton()->wait_for_task_completion(task);
		task = WorkerThreadPool::INVALID_TASK_ID;
		memdelete(job);
		job = nullptr;
	}
	_clear();

	job = memnew(Job);
	job->generate_lods = generate_lods;
	job->make_occluders = occluders_enabled;
	HashMap<RID, int> mesh_surfaces;
	HashMap<GroupKey, int, GroupKey> groups;
	pending_sources.clear();
	for (int i = 0; i < get_child_count(); i++) {
		_gather(get_child(i), *job, mesh_surfaces, groups, pending_sources);
	}
	task = WorkerThreadPool::get_singleton()->add_native_task(&StaticScenery3D::_build, job, true, "Merge static scenery");
	set_process_internal(true);
}

void StaticScenery3D::_gather(Node *p_node, Job &r_job, HashMap<RID, int> &r_mesh_surfaces, HashMap<GroupKey, int, GroupKey> &r_groups, LocalVector<ObjectID> &r_sources) {
	if (Object::cast_to<StaticScenery3D>(p_node) != nullptr) {
		return; // Scenery inside scenery merges its own.
	}
	GeometryInstance3D *instance = Object::cast_to<GeometryInstance3D>(p_node);
	const bool can_merge = instance != nullptr && _shown_under_scenery(instance) && instance->get_material_overlay().is_null() &&
			instance->get_visibility_range_begin() == 0.0 && instance->get_visibility_range_end() == 0.0 &&
			!_has_instance_parameters(instance);
	const Transform3D to_scenery = get_global_transform().affine_inverse() * (instance != nullptr ? instance->get_global_transform() : Transform3D());

	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_node);
	MultiMeshInstance3D *multimesh_instance = Object::cast_to<MultiMeshInstance3D>(p_node);
	if (can_merge && mesh_instance != nullptr && mesh_instance->get_mesh().is_valid() && mesh_instance->get_skin().is_null()) {
		const Ref<Mesh> mesh = mesh_instance->get_mesh();
		bool mergeable = mesh->get_blend_shape_count() == 0;
		for (int s = 0; mergeable && s < mesh->get_surface_count(); s++) {
			mergeable = mesh->surface_get_primitive_type(s) == Mesh::PRIMITIVE_TRIANGLES && (mesh->surface_get_format(s) & Mesh::ARRAY_FORMAT_BONES) == 0 && !is_transparent(mesh_instance->get_active_material(s));
		}
		if (mergeable && mesh->get_surface_count() > 0) {
			_add_surfaces(mesh_instance, mesh, to_scenery, r_job, r_mesh_surfaces, r_groups);
			r_sources.push_back(mesh_instance->get_instance_id());
		}
	} else if (can_merge && multimesh_instance != nullptr && multimesh_instance->get_multimesh().is_valid()) {
		const Ref<MultiMesh> multimesh = multimesh_instance->get_multimesh();
		const Ref<Mesh> mesh = multimesh->get_mesh();
		bool mergeable = mesh.is_valid() && mesh->get_blend_shape_count() == 0 && multimesh->get_transform_format() == MultiMesh::TRANSFORM_3D && !multimesh->is_using_colors() && !multimesh->is_using_custom_data();
		for (int s = 0; mergeable && s < mesh->get_surface_count(); s++) {
			const Ref<Material> material = multimesh_instance->get_material_override().is_valid() ? multimesh_instance->get_material_override() : mesh->surface_get_material(s);
			mergeable = mesh->surface_get_primitive_type(s) == Mesh::PRIMITIVE_TRIANGLES && (mesh->surface_get_format(s) & Mesh::ARRAY_FORMAT_BONES) == 0 && !is_transparent(material);
		}
		const int count = mergeable ? (multimesh->get_visible_instance_count() >= 0 ? multimesh->get_visible_instance_count() : multimesh->get_instance_count()) : 0;
		int64_t vertices = 0;
		for (int s = 0; mergeable && s < mesh->get_surface_count(); s++) {
			vertices += int64_t(mesh->surface_get_array_len(s)) * count;
		}
		mergeable = mergeable && vertices <= MULTIMESH_MERGE_VERTICES;
		if (mergeable && mesh->get_surface_count() > 0) {
			for (int i = 0; i < count; i++) {
				_add_surfaces(multimesh_instance, mesh, to_scenery * multimesh->get_instance_transform(i), r_job, r_mesh_surfaces, r_groups);
			}
			r_sources.push_back(multimesh_instance->get_instance_id());
		}
	}

	for (int i = 0; i < p_node->get_child_count(); i++) {
		_gather(p_node->get_child(i), r_job, r_mesh_surfaces, r_groups, r_sources);
	}
}

void StaticScenery3D::_add_surfaces(GeometryInstance3D *p_instance, const Ref<Mesh> &p_mesh, const Transform3D &p_transform, Job &r_job, HashMap<RID, int> &r_mesh_surfaces, HashMap<GroupKey, int, GroupKey> &r_groups) {
	// Each mesh's surfaces are read once (from the renderer, for most meshes), however many use it.
	const RID mesh_rid = p_mesh->get_rid();
	if (!r_mesh_surfaces.has(mesh_rid)) {
		r_mesh_surfaces[mesh_rid] = r_job.surfaces.size();
		for (int s = 0; s < p_mesh->get_surface_count(); s++) {
			r_job.surfaces.push_back(p_mesh->surface_get_arrays(s));
		}
	}
	const int first_surface = r_mesh_surfaces[mesh_rid];
	const AABB aabb = p_transform.xform(p_mesh->get_aabb());
	const Vector3 centre = aabb.get_center();
	const Vector3i chunk = chunk_size > 0.0 ? Vector3i((centre / chunk_size).floor()) : Vector3i();
	const bool occluder = occludes(aabb);
	MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_instance);

	for (int s = 0; s < p_mesh->get_surface_count(); s++) {
		Ref<Material> material;
		if (mesh_instance != nullptr) {
			material = mesh_instance->get_active_material(s);
		} else {
			material = p_instance->get_material_override().is_valid() ? p_instance->get_material_override() : p_mesh->surface_get_material(s);
		}
		GroupKey key;
		key.material = material.is_valid() ? material->get_rid() : RID();
		key.cast_shadow = p_instance->get_cast_shadows_setting();
		key.layers = p_instance->get_layer_mask();
		key.gi_mode = p_instance->get_gi_mode();
		key.chunk = chunk;
		if (!r_groups.has(key)) {
			r_groups[key] = r_job.groups.size();
			Group group;
			group.key = key;
			group.material = material;
			r_job.groups.push_back(group);
		}
		Piece piece;
		piece.surface = first_surface + s;
		piece.transform = p_transform;
		piece.occludes = occluder && !is_transparent(material);
		r_job.groups[r_groups[key]].pieces.push_back(piece);
	}
}

// In the background: each group's pieces into one surface, with simpler versions for distance,
// and the occluder of its large pieces.
void StaticScenery3D::_build(void *p_job) {
	Job *job = static_cast<Job *>(p_job);
	for (Group &group : job->groups) {
		bool has_normal = false;
		bool has_tangent = false;
		bool has_color = false;
		bool has_uv = false;
		bool has_uv2 = false;
		int vertex_count = 0;
		for (const Piece &piece : group.pieces) {
			const Array &arrays = job->surfaces[piece.surface];
			has_normal = has_normal || !PackedVector3Array(arrays[Mesh::ARRAY_NORMAL]).is_empty();
			has_tangent = has_tangent || !PackedFloat32Array(arrays[Mesh::ARRAY_TANGENT]).is_empty();
			has_color = has_color || !PackedColorArray(arrays[Mesh::ARRAY_COLOR]).is_empty();
			has_uv = has_uv || !PackedVector2Array(arrays[Mesh::ARRAY_TEX_UV]).is_empty();
			has_uv2 = has_uv2 || !PackedVector2Array(arrays[Mesh::ARRAY_TEX_UV2]).is_empty();
			vertex_count += PackedVector3Array(arrays[Mesh::ARRAY_VERTEX]).size();
		}
		if (vertex_count == 0) {
			continue;
		}

		PackedVector3Array vertices;
		PackedVector3Array normals;
		PackedFloat32Array tangents;
		PackedColorArray colors;
		PackedVector2Array uvs;
		PackedVector2Array uv2s;
		PackedInt32Array indices;
		vertices.resize(vertex_count);
		if (has_normal) {
			normals.resize(vertex_count);
		}
		if (has_tangent) {
			tangents.resize(vertex_count * 4);
		}
		if (has_color) {
			colors.resize(vertex_count);
		}
		if (has_uv) {
			uvs.resize(vertex_count);
		}
		if (has_uv2) {
			uv2s.resize(vertex_count);
		}
		int at = 0;
		for (const Piece &piece : group.pieces) {
			const Array &arrays = job->surfaces[piece.surface];
			const PackedVector3Array source_vertices = arrays[Mesh::ARRAY_VERTEX];
			const PackedVector3Array source_normals = arrays[Mesh::ARRAY_NORMAL];
			const PackedFloat32Array source_tangents = arrays[Mesh::ARRAY_TANGENT];
			const PackedColorArray source_colors = arrays[Mesh::ARRAY_COLOR];
			const PackedVector2Array source_uvs = arrays[Mesh::ARRAY_TEX_UV];
			const PackedVector2Array source_uv2s = arrays[Mesh::ARRAY_TEX_UV2];
			const PackedInt32Array source_indices = arrays[Mesh::ARRAY_INDEX];
			const int count = source_vertices.size();
			const Basis normal_basis = piece.transform.basis.inverse().transposed();
			const bool mirrored = piece.transform.basis.determinant() < 0.0;
			for (int v = 0; v < count; v++) {
				vertices.set(at + v, piece.transform.xform(source_vertices[v]));
				if (has_normal) {
					normals.set(at + v, source_normals.size() == count ? normal_basis.xform(source_normals[v]).normalized() : Vector3(0, 1, 0));
				}
				if (has_tangent) {
					Vector3 tangent(1, 0, 0);
					float sign = 1.0;
					if (source_tangents.size() == count * 4) {
						tangent = piece.transform.basis.xform(Vector3(source_tangents[v * 4], source_tangents[v * 4 + 1], source_tangents[v * 4 + 2])).normalized();
						sign = source_tangents[v * 4 + 3] * (mirrored ? -1.0 : 1.0);
					}
					tangents.set((at + v) * 4, tangent.x);
					tangents.set((at + v) * 4 + 1, tangent.y);
					tangents.set((at + v) * 4 + 2, tangent.z);
					tangents.set((at + v) * 4 + 3, sign);
				}
				if (has_color) {
					colors.set(at + v, source_colors.size() == count ? source_colors[v] : Color(1, 1, 1));
				}
				if (has_uv) {
					uvs.set(at + v, source_uvs.size() == count ? source_uvs[v] : Vector2());
				}
				if (has_uv2) {
					uv2s.set(at + v, source_uv2s.size() == count ? source_uv2s[v] : Vector2());
				}
			}
			const int index_count = source_indices.is_empty() ? count : source_indices.size();
			const int first_index = indices.size();
			indices.resize(first_index + index_count);
			int *out = indices.ptrw() + first_index;
			for (int i = 0; i < index_count; i++) {
				out[i] = at + (source_indices.is_empty() ? i : source_indices[i]);
			}
			if (mirrored) { // Mirrored copies turn the other way round: keep them facing out.
				for (int i = 0; i + 2 < index_count; i += 3) {
					SWAP(out[i + 1], out[i + 2]);
				}
			}

			if (piece.occludes && job->make_occluders) {
				const int occluder_first = group.occluder_vertices.size();
				group.occluder_vertices.resize(occluder_first + count);
				for (int v = 0; v < count; v++) {
					group.occluder_vertices.set(occluder_first + v, vertices[at + v]);
				}
				const int occluder_index_first = group.occluder_indices.size();
				group.occluder_indices.resize(occluder_index_first + index_count);
				for (int i = 0; i < index_count; i++) {
					group.occluder_indices.set(occluder_index_first + i, occluder_first + (out[i] - at));
				}
			}
			at += count;
		}

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = vertices;
		arrays[Mesh::ARRAY_INDEX] = indices;
		if (has_normal) {
			arrays[Mesh::ARRAY_NORMAL] = normals;
		}
		if (has_tangent) {
			arrays[Mesh::ARRAY_TANGENT] = tangents;
		}
		if (has_color) {
			arrays[Mesh::ARRAY_COLOR] = colors;
		}
		if (has_uv) {
			arrays[Mesh::ARRAY_TEX_UV] = uvs;
		}
		if (has_uv2) {
			arrays[Mesh::ARRAY_TEX_UV2] = uv2s;
		}
		group.merged.instantiate();
		group.merged->add_surface(Mesh::PRIMITIVE_TRIANGLES, arrays, TypedArray<Array>(), Dictionary(), group.material);
		if (job->generate_lods && has_normal) {
			group.merged->generate_lods(LOD_NORMAL_MERGE_ANGLE, Array());
		}

		// Occluders need few triangles: their surface is simplified, keeping its outline.
		if (group.occluder_indices.size() >= 3 && SurfaceTool::simplify_func != nullptr && SurfaceTool::simplify_scale_func != nullptr) {
			const Vector<float> points = vector3_to_float32_array(group.occluder_vertices.ptr(), group.occluder_vertices.size());
			const float scale = SurfaceTool::simplify_scale_func(points.ptr(), group.occluder_vertices.size(), sizeof(float) * 3);
			PackedInt32Array simplified;
			simplified.resize(group.occluder_indices.size());
			float error = 0.0;
			const size_t kept = SurfaceTool::simplify_func((unsigned int *)simplified.ptrw(), (const unsigned int *)group.occluder_indices.ptr(), group.occluder_indices.size(),
					points.ptr(), group.occluder_vertices.size(), sizeof(float) * 3, 36, OCCLUDER_SIMPLIFICATION / MAX(scale, 0.0001f), SurfaceTool::SIMPLIFY_LOCK_BORDER, &error);
			simplified.resize(kept);
			group.occluder_indices = simplified;
		}
	}
}

void StaticScenery3D::_check_job() {
	if (task == WorkerThreadPool::INVALID_TASK_ID || !WorkerThreadPool::get_singleton()->is_task_completed(task)) {
		return;
	}
	WorkerThreadPool::get_singleton()->wait_for_task_completion(task);
	task = WorkerThreadPool::INVALID_TASK_ID;
	set_process_internal(false);

	RenderingServer *rs = RS::get_singleton();
	const RID scenario = get_world_3d()->get_scenario();
	const Transform3D transform = get_global_transform();
	const bool visible = is_visible_in_tree();
	for (Group &group : job->groups) {
		if (group.merged.is_null()) {
			continue;
		}
		Drawn each;
		each.mesh = group.merged->get_mesh();
		each.instance = rs->instance_create2(each.mesh->get_rid(), scenario);
		rs->instance_set_transform(each.instance, transform);
		rs->instance_set_layer_mask(each.instance, group.key.layers);
		rs->instance_geometry_set_cast_shadows_setting(each.instance, RSE::ShadowCastingSetting(group.key.cast_shadow));
		rs->instance_geometry_set_flag(each.instance, RSE::INSTANCE_FLAG_USE_BAKED_LIGHT, group.key.gi_mode == GeometryInstance3D::GI_MODE_STATIC);
		rs->instance_geometry_set_flag(each.instance, RSE::INSTANCE_FLAG_USE_DYNAMIC_GI, group.key.gi_mode == GeometryInstance3D::GI_MODE_DYNAMIC);
		rs->instance_attach_object_instance_id(each.instance, get_instance_id());
		rs->instance_set_visible(each.instance, visible);
		if (group.occluder_indices.size() >= 3) {
			each.occluder = rs->occluder_create();
			rs->occluder_set_mesh(each.occluder, group.occluder_vertices, group.occluder_indices);
			each.occluder_instance = rs->instance_create2(each.occluder, scenario);
			rs->instance_set_transform(each.occluder_instance, transform);
			rs->instance_set_visible(each.occluder_instance, visible);
		}
		drawn.push_back(each);
	}
	for (const ObjectID &id : pending_sources) {
		GeometryInstance3D *original = ObjectDB::get_instance<GeometryInstance3D>(id);
		if (original != nullptr) {
			rs->instance_set_visible(original->get_instance(), false);
			hidden.push_back(id);
		}
	}
	pending_sources.clear();
	memdelete(job);
	job = nullptr;
	merged = true;
	set_notify_transform(true);
	emit_signal(SNAME("merged"));
}

void StaticScenery3D::set_chunk_size(float p_size) {
	chunk_size = MAX(p_size, 0.0f);
}

float StaticScenery3D::get_chunk_size() const {
	return chunk_size;
}

void StaticScenery3D::set_generate_lods(bool p_generate) {
	generate_lods = p_generate;
}

bool StaticScenery3D::is_generating_lods() const {
	return generate_lods;
}

void StaticScenery3D::set_occluders_enabled(bool p_enabled) {
	occluders_enabled = p_enabled;
}

bool StaticScenery3D::is_occluders_enabled() const {
	return occluders_enabled;
}

bool StaticScenery3D::is_merged() const {
	return merged;
}

TypedArray<ArrayMesh> StaticScenery3D::get_merged_meshes() const {
	TypedArray<ArrayMesh> meshes;
	for (const Drawn &each : drawn) {
		meshes.push_back(each.mesh);
	}
	return meshes;
}

void StaticScenery3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_chunk_size", "size"), &StaticScenery3D::set_chunk_size);
	ClassDB::bind_method(D_METHOD("get_chunk_size"), &StaticScenery3D::get_chunk_size);
	ClassDB::bind_method(D_METHOD("set_generate_lods", "generate"), &StaticScenery3D::set_generate_lods);
	ClassDB::bind_method(D_METHOD("is_generating_lods"), &StaticScenery3D::is_generating_lods);
	ClassDB::bind_method(D_METHOD("set_occluders_enabled", "enabled"), &StaticScenery3D::set_occluders_enabled);
	ClassDB::bind_method(D_METHOD("is_occluders_enabled"), &StaticScenery3D::is_occluders_enabled);
	ClassDB::bind_method(D_METHOD("merge"), &StaticScenery3D::merge);
	ClassDB::bind_method(D_METHOD("is_merged"), &StaticScenery3D::is_merged);
	ClassDB::bind_method(D_METHOD("get_merged_meshes"), &StaticScenery3D::get_merged_meshes);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "chunk_size", PROPERTY_HINT_RANGE, "0,1024,1,or_greater,suffix:m"), "set_chunk_size", "get_chunk_size");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "generate_lods"), "set_generate_lods", "is_generating_lods");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "occluders_enabled"), "set_occluders_enabled", "is_occluders_enabled");

	ADD_SIGNAL(MethodInfo("merged"));
}
