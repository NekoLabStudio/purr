#include "terrain_occluders.h"

#include "core/io/image.h"
#include "servers/rendering/rendering_server.h"

#include "terrain_3d.h"
#include "terrain_3d_data.h"
#include "terrain_3d_region.h"
#include "terrain_3d_util.h"

#include <cmath>

void TerrainOccluders::set_scenario(RID p_scenario) {
	scenario = p_scenario;
	for (KeyValue<Vector2i, Region> &region : regions) {
		RS::get_singleton()->instance_set_scenario(region.value.instance, scenario);
	}
}

void TerrainOccluders::set_visible(bool p_visible) {
	visible = p_visible;
	for (KeyValue<Vector2i, Region> &region : regions) {
		RS::get_singleton()->instance_set_visible(region.value.instance, visible);
	}
}

void TerrainOccluders::mark_dirty(const AABB &p_area, real_t p_region_width) {
	if (p_region_width <= 0.0) {
		return;
	}
	// A point on a region's edge is in both regions' grids.
	const Vector2i from = Vector2i(Math::floor((p_area.position.x - 1.0) / p_region_width), Math::floor((p_area.position.z - 1.0) / p_region_width));
	const Vector2i to = Vector2i(Math::floor((p_area.get_end().x + 1.0) / p_region_width), Math::floor((p_area.get_end().z + 1.0) / p_region_width));
	for (int z = from.y; z <= to.y; z++) {
		for (int x = from.x; x <= to.x; x++) {
			dirty.insert(Vector2i(x, z));
		}
	}
}

void TerrainOccluders::_free(Region &p_region) {
	RS::get_singleton()->free_rid(p_region.instance);
	RS::get_singleton()->free_rid(p_region.occluder);
}

void TerrainOccluders::clear() {
	if (RS::get_singleton() != nullptr) {
		for (KeyValue<Vector2i, Region> &region : regions) {
			_free(region.value);
		}
	}
	regions.clear();
	dirty.clear();
	all_dirty = true;
	regions_changed = true;
}

void TerrainOccluders::update(Terrain3D *p_terrain) {
	const Terrain3DData *data = p_terrain->get_data();
	if (data == nullptr) {
		return;
	}
	HashSet<Vector2i> loaded;
	const TypedArray<Vector2i> locations = data->get_region_locations();
	for (int i = 0; i < locations.size(); i++) {
		const Vector2i location = locations[i];
		const Terrain3DRegion *region = data->get_region_ptr(location);
		if (region != nullptr && !region->is_deleted()) {
			loaded.insert(location);
		}
	}
	LocalVector<Vector2i> gone;
	for (const KeyValue<Vector2i, Region> &region : regions) {
		if (!loaded.has(region.key)) {
			gone.push_back(region.key);
		}
	}
	for (const Vector2i &location : gone) {
		_free(regions[location]);
		regions.erase(location);
	}
	for (const Vector2i &location : loaded) {
		const bool built = regions.has(location);
		const Ref<Image> heights = data->get_region_ptr(location)->get_height_map();
		const ObjectID heights_id = heights.is_valid() ? heights->get_instance_id() : ObjectID();
		if (!built || all_dirty || dirty.has(location) || regions[location].heights != heights_id) {
			if (!built) {
				Region region;
				region.occluder = RS::get_singleton()->occluder_create();
				region.instance = RS::get_singleton()->instance_create();
				RS::get_singleton()->instance_set_base(region.instance, region.occluder);
				RS::get_singleton()->instance_set_scenario(region.instance, scenario);
				RS::get_singleton()->instance_set_visible(region.instance, visible);
				regions.insert(location, region);
			}
			_build(p_terrain, location, regions[location]);
			regions[location].heights = heights_id;
		}
	}
	all_dirty = false;
	regions_changed = false;
	dirty.clear();
}

void TerrainOccluders::_build(Terrain3D *p_terrain, const Vector2i &p_location, Region &r_region) {
	const Terrain3DRegion *region = p_terrain->get_data()->get_region_ptr(p_location);
	const Ref<Image> heights = region->get_height_map();
	const Ref<Image> control = region->get_control_map();
	const int size = p_terrain->get_region_size();
	PackedVector3Array vertices;
	PackedInt32Array indices;
	if (heights.is_null() || heights->get_format() != Image::FORMAT_RF || heights->get_width() < size || heights->get_height() < size) {
		RS::get_singleton()->occluder_set_mesh(r_region.occluder, vertices, indices);
		return;
	}
	const bool has_control = control.is_valid() && control->get_format() == Image::FORMAT_RF && control->get_width() >= size && control->get_height() >= size;
	const PackedByteArray height_bytes = heights->get_data();
	const PackedByteArray control_bytes = has_control ? control->get_data() : PackedByteArray();
	const float *height = reinterpret_cast<const float *>(height_bytes.ptr());
	const float *controls = has_control ? reinterpret_cast<const float *>(control_bytes.ptr()) : nullptr;
	const int width = heights->get_width();
	const int control_width = has_control ? control->get_width() : 0;
	const int step = MAX(size / CELLS, 1);
	const int cells = size / step;

	// Each cell's lowest ground, and whether it has a hole (no occluder there).
	LocalVector<float> lowest;
	LocalVector<uint8_t> holed;
	lowest.resize(cells * cells);
	holed.resize(cells * cells);
	for (int cz = 0; cz < cells; cz++) {
		for (int cx = 0; cx < cells; cx++) {
			float low = FLT_MAX;
			bool hole = false;
			// The cell's pixels and its far edge, which the next cell's grid point shares.
			for (int z = cz * step; z <= MIN((cz + 1) * step, size - 1); z++) {
				for (int x = cx * step; x <= MIN((cx + 1) * step, size - 1); x++) {
					const float h = height[z * width + x];
					if (std::isnan(h)) {
						hole = true;
						continue;
					}
					low = MIN(low, h);
					if (controls != nullptr && is_hole(controls[z * control_width + x])) {
						hole = true;
					}
				}
			}
			lowest[cz * cells + cx] = low;
			holed[cz * cells + cx] = hole || low == FLT_MAX;
		}
	}

	// A grid point is as low as the lowest of the cells round it, so the triangles between points
	// never rise above the ground they cover.
	const real_t spacing = p_terrain->get_vertex_spacing();
	vertices.resize((cells + 1) * (cells + 1));
	Vector3 *points = vertices.ptrw();
	for (int z = 0; z <= cells; z++) {
		for (int x = 0; x <= cells; x++) {
			float low = FLT_MAX;
			for (int dz = -1; dz <= 0; dz++) {
				for (int dx = -1; dx <= 0; dx++) {
					const int cx = x + dx;
					const int cz = z + dz;
					if (cx >= 0 && cz >= 0 && cx < cells && cz < cells && !holed[cz * cells + cx]) {
						low = MIN(low, lowest[cz * cells + cx]);
					}
				}
			}
			points[z * (cells + 1) + x] = Vector3(x * step * spacing, low == FLT_MAX ? 0.0f : low - SINK, z * step * spacing);
		}
	}
	for (int cz = 0; cz < cells; cz++) {
		for (int cx = 0; cx < cells; cx++) {
			if (holed[cz * cells + cx]) {
				continue;
			}
			const int a = cz * (cells + 1) + cx;
			const int b = a + 1;
			const int c = a + cells + 1;
			const int d = c + 1;
			indices.append_array({ a, b, c, b, d, c });
		}
	}
	RS::get_singleton()->occluder_set_mesh(r_region.occluder, vertices, indices);

	const real_t region_width = size * spacing;
	const Vector3 map_position = Vector3(p_location.x * region_width, 0.0, p_location.y * region_width);
	RS::get_singleton()->instance_set_transform(r_region.instance, Transform3D(Basis(), map_position - Terrain3D::get_map_offset()));
}
