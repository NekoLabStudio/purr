#pragma once

// StaticScenery3D: the scenery under it that never moves (buildings, rocks, fences, props) drawn
// as a few merged meshes. In a running game it gathers the MeshInstance3D and MultiMeshInstance3D
// nodes below it and, in the background, merges their meshes by material within each chunk of
// the world: one draw per material per chunk instead of one per object. Each merged mesh gets
// simpler versions for distance (Godot picks them as the chunk gets smaller on screen), and each
// chunk an occluder made from its large pieces, so they hide what's behind them. Then it draws
// the merged meshes and hides the originals, which stay in the tree as they were (collision,
// scripts and child nodes keep working). In the editor nothing is merged.

#include "core/object/worker_thread_pool.h"
#include "core/templates/hash_map.h"
#include "scene/3d/node_3d.h"
#include "scene/3d/visual_instance_3d.h"
#include "scene/resources/mesh.h"

class ImporterMesh;

class StaticScenery3D : public Node3D {
	GDCLASS(StaticScenery3D, Node3D);

public:
	// What draws together: the same material, drawn the same way, in the same chunk.
	struct GroupKey {
		RID material;
		int cast_shadow = 0;
		uint32_t layers = 1;
		int gi_mode = 0;
		Vector3i chunk;

		bool operator==(const GroupKey &p_other) const {
			return material == p_other.material && cast_shadow == p_other.cast_shadow && layers == p_other.layers && gi_mode == p_other.gi_mode && chunk == p_other.chunk;
		}
		static uint32_t hash(const GroupKey &p_key) {
			uint32_t h = hash_murmur3_one_64(p_key.material.get_id());
			h = hash_murmur3_one_32(p_key.cast_shadow, h);
			h = hash_murmur3_one_32(p_key.layers, h);
			h = hash_murmur3_one_32(p_key.gi_mode, h);
			h = hash_murmur3_one_32(p_key.chunk.x, h);
			h = hash_murmur3_one_32(p_key.chunk.y, h);
			h = hash_murmur3_one_32(p_key.chunk.z, h);
			return hash_fmix32(h);
		}
	};

	// A surface of a mesh, placed in the scenery's space.
	struct Piece {
		int surface = -1; // Into the job's surfaces.
		Transform3D transform;
		bool occludes = false;
	};

	struct Group {
		GroupKey key;
		Ref<Material> material;
		LocalVector<Piece> pieces;
		// Made in the background:
		Ref<ImporterMesh> merged;
		PackedVector3Array occluder_vertices;
		PackedInt32Array occluder_indices;
	};

	struct Job {
		LocalVector<Array> surfaces; // Each mesh's surfaces once, however many use them.
		LocalVector<Group> groups;
		bool generate_lods = true;
		bool make_occluders = true;
	};

private:
	struct Drawn {
		RID instance;
		Ref<ArrayMesh> mesh;
		RID occluder;
		RID occluder_instance;
	};

	float chunk_size = 64.0;
	bool generate_lods = true;
	bool occluders_enabled = true;

	LocalVector<ObjectID> hidden; // The originals, hidden on the server while the merged meshes draw.
	LocalVector<Drawn> drawn;
	Job *job = nullptr;
	WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
	bool merged = false;

	static void _build(void *p_job);
	void _gather(Node *p_node, Job &r_job, HashMap<RID, int> &r_mesh_surfaces, HashMap<GroupKey, int, GroupKey> &r_groups, LocalVector<ObjectID> &r_sources);
	void _add_surfaces(GeometryInstance3D *p_instance, const Ref<Mesh> &p_mesh, const Transform3D &p_transform, Job &r_job, HashMap<RID, int> &r_mesh_surfaces, HashMap<GroupKey, int, GroupKey> &r_groups);
	void _check_job();
	void _show(bool p_merged_visible);
	void _clear();

	LocalVector<ObjectID> pending_sources;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_chunk_size(float p_size);
	float get_chunk_size() const;
	void set_generate_lods(bool p_generate);
	bool is_generating_lods() const;
	void set_occluders_enabled(bool p_enabled);
	bool is_occluders_enabled() const;

	void merge();
	bool is_merged() const;
	TypedArray<ArrayMesh> get_merged_meshes() const;

	~StaticScenery3D();
};
