# Checks Terrain3D. Exits non-zero if a check fails:
#   purr --headless --path samples/terrain -s test.gd
extends SceneTree

var failures := 0


func check(what: String, ok: bool, detail: Variant = null) -> void:
	print("check: ", what, "" if ok else "  FAILED  %s" % [detail])
	if not ok:
		failures += 1


func frames(count: int) -> void:
	for i in count:
		await physics_frame


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var terrain := Terrain3D.new()
	root.add_child(terrain)
	await frames(2)
	check("a terrain starts with data", terrain.data != null)
	var region: Terrain3DRegion = terrain.data.add_region_blank(Vector2i.ZERO)
	check("a blank region can be added", region != null and terrain.data.get_region_count() == 1, terrain.data.get_region_count())
	terrain.data.set_height(Vector3(10, 0, 10), 25.0)
	terrain.data.update_maps()
	check("heights are kept", is_equal_approx(terrain.data.get_height(Vector3(10, 0, 10)), 25.0), terrain.data.get_height(Vector3(10, 0, 10)))
	var map: PackedInt32Array = terrain.data.get_region_map()
	var marked := 0
	for value in map:
		if value != 0:
			marked += 1
	check("the region map marks only real regions", marked == terrain.data.get_region_count(), marked)
	check("outside a region there's no height", is_nan(terrain.data.get_height(Vector3(-5000, 0, -5000))))

	var camera := Camera3D.new()
	root.add_child(camera)
	camera.make_current()
	await frames(2)
	check("a camera added later is followed", terrain.get_camera() == camera, terrain.get_camera())

	terrain.ocean_enabled = true
	await frames(1)
	check("turning the ocean on gives it a material to edit", terrain.ocean_material is ShaderMaterial, terrain.ocean_material)

	var grass := Terrain3DTextureAsset.new()
	var mud := Terrain3DTextureAsset.new()
	mud.id = 1
	terrain.assets.set_texture_list([grass])
	terrain.assets.set_texture_list([grass, mud])
	check("adding a texture keeps the others' IDs", grass.id == 0 and mud.id == 1, [grass.id, mud.id])
	check("an empty texture stays empty", grass.albedo_texture == null)

	await _shifted()
	await _streaming()
	await _navigation()
	print("terrain: %d failed" % failures if failures else "terrain: all passed")
	quit(1 if failures else 0)


# With a floating origin, the ground stays where it is on the map as the world shifts under it.
func _shifted() -> void:
	var world := Node3D.new()
	root.add_child(world)
	current_scene = world
	WorldOrigin.world = world
	var terrain := Terrain3D.new()
	world.add_child(terrain)
	var camera := Camera3D.new()
	world.add_child(camera)
	camera.make_current()
	camera.position = Vector3(1100, 40, 100)
	await frames(2)
	terrain.data.add_region_blank(Vector2i(4, 0))
	for x in range(1050, 1150):
		for z in range(50, 150):
			terrain.data.set_height(Vector3(x, 0, z), 25.0)
	terrain.data.update_maps()
	await frames(5)
	check("before a shift, the ground is hit where it is", is_equal_approx(ground_at(Vector3(1100, 0, 100)), 25.0), ground_at(Vector3(1100, 0, 100)))

	WorldOrigin.shift(Vector3(-1000, 0, 0))
	await frames(5)
	check("the camera moved with the world", camera.global_position.is_equal_approx(Vector3(100, 40, 100)), camera.global_position)
	check("after a shift, the ground is hit where it moved to", is_equal_approx(ground_at(Vector3(100, 0, 100)), 25.0), ground_at(Vector3(100, 0, 100)))
	var hit := terrain.get_intersection(Vector3(100, 60, 100), Vector3.DOWN)
	check("and get_intersection() finds it there", hit.is_equal_approx(Vector3(100, 25, 100)), hit)
	camera.position.x += 60
	await frames(5)
	check("collision follows the camera after a shift", is_equal_approx(ground_at(Vector3(140, 0, 100)), 25.0), ground_at(Vector3(140, 0, 100)))
	WorldOrigin.reset()
	world.queue_free()
	await frames(1)


# Regions near the camera load as it nears them, and those far behind it are dropped.
func _streaming() -> void:
	var folder := "user://streaming_test"
	DirAccess.make_dir_recursive_absolute(folder)
	for file in DirAccess.get_files_at(folder):
		DirAccess.remove_absolute(folder.path_join(file))
	var maker := Terrain3D.new()
	root.add_child(maker)
	await frames(2)
	for x in 6:
		maker.data.add_region_blank(Vector2i(x, 0), false)
		maker.data.set_height(Vector3(x * 256 + 100, 0, 100), 10.0 * (x + 1))
	maker.data.save_directory(folder)
	maker.queue_free()
	await frames(1)
	check("a strip of regions is saved", DirAccess.get_files_at(folder).size() == 6, DirAccess.get_files_at(folder))

	var camera := Camera3D.new()
	camera.position = Vector3(128, 50, 128)
	root.add_child(camera)
	camera.make_current()
	var terrain := Terrain3D.new()
	terrain.data_directory = folder
	terrain.region_load_radius = 300.0
	root.add_child(terrain)
	await frames(3)
	check("streaming in a game with a load radius", terrain.is_streaming())
	check("only the regions near the camera load", loaded(terrain) == [Vector2i(0, 0), Vector2i(1, 0)], loaded(terrain))
	check("with their ground", is_equal_approx(terrain.data.get_height(Vector3(100, 0, 100)), 10.0), terrain.data.get_height(Vector3(100, 0, 100)))

	camera.position.x = 1100
	var started := Time.get_ticks_msec()
	while loaded(terrain) != [Vector2i(3, 0), Vector2i(4, 0), Vector2i(5, 0)] and Time.get_ticks_msec() - started < 5000:
		await physics_frame
	check("moving on, the regions ahead load and those behind go", loaded(terrain) == [Vector2i(3, 0), Vector2i(4, 0), Vector2i(5, 0)], loaded(terrain))
	check("the ground ahead is there", is_equal_approx(terrain.data.get_height(Vector3(4 * 256 + 100, 0, 100)), 50.0), terrain.data.get_height(Vector3(4 * 256 + 100, 0, 100)))
	check("dropping a region leaves its file", DirAccess.get_files_at(folder).size() == 6)

	terrain.load_regions_around(Vector3(128, 0, 128))
	check("load_regions_around() reads them at once", terrain.data.has_region(Vector2i(0, 0)))

	# Another level: a folder with one region, higher.
	var other := "user://streaming_test_other"
	DirAccess.make_dir_recursive_absolute(other)
	for file in DirAccess.get_files_at(other):
		DirAccess.remove_absolute(other.path_join(file))
	var maker_other := Terrain3D.new()
	root.add_child(maker_other)
	await frames(2)
	maker_other.data.add_region_blank(Vector2i(0, 0), false)
	maker_other.data.set_height(Vector3(100, 0, 100), 99.0)
	maker_other.data.save_directory(other)
	maker_other.queue_free()
	camera.position.x = 128
	terrain.data_directory = other
	await frames(3)
	check("switching folders streams from the new one", loaded(terrain) == [Vector2i(0, 0)] and is_equal_approx(terrain.data.get_height(Vector3(100, 0, 100)), 99.0), [loaded(terrain), terrain.data.get_height(Vector3(100, 0, 100))])
	await frames(10)
	check("and nothing from the old one joins later", loaded(terrain) == [Vector2i(0, 0)], loaded(terrain))
	terrain.queue_free()
	camera.queue_free()
	await frames(1)


# A NavigationRegion3D's bake covers the terrain under it: all of it, or the parts painted for
# navigation once any are.
func _navigation() -> void:
	var region := NavigationRegion3D.new()
	region.navigation_mesh = NavigationMesh.new()
	root.add_child(region)
	var terrain := Terrain3D.new()
	region.add_child(terrain)
	await frames(2)
	terrain.data.add_region_blank(Vector2i.ZERO)
	region.bake_navigation_mesh(false)
	var everywhere := extent(region.navigation_mesh)
	check("a bake covers the terrain", everywhere.size.x > 200 and everywhere.size.z > 200, everywhere)
	for x in range(20, 60):
		for z in range(20, 60):
			terrain.data.set_control_navigation(Vector3(x, 0, z), true)
	terrain.data.update_maps()
	region.bake_navigation_mesh(false)
	var painted := extent(region.navigation_mesh)
	check("or only the parts painted for navigation", painted.size.x < 60 and painted.size.z < 60 and painted.position.x > 10, painted)

	# A region away from the origin, baking only within a box round its middle.
	for x in range(0, 256):
		for z in range(0, 256):
			terrain.data.set_control_navigation(Vector3(x, 0, z), false)
	terrain.data.update_maps()
	terrain.reparent(root)
	region.position = Vector3(100, 0, 100)
	terrain.reparent(region)
	region.navigation_mesh.filter_baking_aabb = AABB(Vector3(-20, -10, -20), Vector3(40, 20, 40))
	region.bake_navigation_mesh(false)
	var boxed := extent(region.navigation_mesh)
	check("a baking box is where the region is", boxed.size.x > 30 and boxed.size.x < 45 and absf(boxed.get_center().x) < 5.0, boxed)
	region.queue_free()
	await frames(1)


func extent(mesh: NavigationMesh) -> AABB:
	var vertices := mesh.get_vertices()
	if vertices.is_empty():
		return AABB()
	var bounds := AABB(vertices[0], Vector3.ZERO)
	for vertex in vertices:
		bounds = bounds.expand(vertex)
	return bounds


func loaded(terrain: Terrain3D) -> Array[Vector2i]:
	var locations: Array[Vector2i] = []
	locations.assign(terrain.data.get_region_locations())
	locations.sort()
	return locations


func ground_at(at: Vector3) -> float:
	var space := root.get_world_3d().direct_space_state
	var hit := space.intersect_ray(PhysicsRayQueryParameters3D.create(at + Vector3.UP * 100.0, at - Vector3.UP * 100.0))
	return hit.position.y if hit else NAN
