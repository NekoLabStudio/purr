#pragma once

// The terrain's occluders, so hills hide what's behind them from the renderer (occlusion
// culling): for each loaded region, a coarse grid laid just under the ground, so it never hides
// anything that's above it. Rebuilt as regions load, unload or change height.

#include "core/math/aabb.h"
#include "core/math/vector2i.h"
#include "core/object/object_id.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/rid.h"

class Terrain3D;

class TerrainOccluders {
	static constexpr int CELLS = 16; // Across each region.
	static constexpr float SINK = 0.5f; // Metres under the lowest ground around each point.

	struct Region {
		RID occluder;
		RID instance;
		ObjectID heights; // The height map it was built from: a region read again has a new one.
	};

	HashMap<Vector2i, Region> regions;
	HashSet<Vector2i> dirty;
	bool all_dirty = true;
	bool regions_changed = true;
	bool visible = true;
	RID scenario;

	void _build(Terrain3D *p_terrain, const Vector2i &p_location, Region &r_region);
	void _free(Region &p_region);

public:
	void set_scenario(RID p_scenario);
	void set_visible(bool p_visible);
	void mark_all_dirty() { all_dirty = true; }
	// Regions loaded, unloaded or read again: those built from the same height map stay.
	void mark_regions_changed() { regions_changed = true; }
	// p_area in map coordinates, as Terrain3DData's maps_edited gives it.
	void mark_dirty(const AABB &p_area, real_t p_region_width);
	bool needs_update() const { return all_dirty || regions_changed || !dirty.is_empty(); }
	// Builds what's missing or changed, and frees regions no longer loaded. Each is placed at its
	// map position minus WorldOrigin's offset; a shift moves them with everything else.
	void update(Terrain3D *p_terrain);
	void clear();

	~TerrainOccluders() { clear(); }
};
