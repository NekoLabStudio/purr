# Checks WorldOrigin. Exits non-zero if a check fails:
#   purr --headless --path samples/world -s test.gd
extends SceneTree

var failures := 0
var shifts: Array[Vector3] = []


func check(what: String, ok: bool, detail: Variant = null) -> void:
	print("check: ", what, "" if ok else "  FAILED  %s" % [detail])
	if not ok:
		failures += 1


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	WorldOrigin.shifted.connect(func(by: Vector3) -> void: shifts.append(by))
	check("starts at the map's own origin", WorldOrigin.offset == Vector3.ZERO and WorldOrigin.to_map(Vector3(5, 0, 5)) == Vector3(5, 0, 5))
	check("the map offset is a shader global", ProjectSettings.has_setting("shader_globals/map_offset"))

	var world := Node3D.new()
	root.add_child(world)
	current_scene = world
	check("the current scene is the world unless one's set", WorldOrigin.world == world)
	var player := Node3D.new()
	world.add_child(player)
	player.global_position = Vector3(2500, 3, -1200)

	WorldOrigin.follow(player.global_position, false)
	check("not calm and within max_distance: no shift", WorldOrigin.offset == Vector3.ZERO)
	WorldOrigin.follow(player.global_position)
	check("calm past calm_distance: the world shifts by whole steps", WorldOrigin.offset == Vector3(3000, 0, -1000), WorldOrigin.offset)
	check("the player is near the origin again", player.global_position.is_equal_approx(Vector3(-500, 3, -200)), player.global_position)
	check("the map position is unchanged", WorldOrigin.to_map(player.global_position).is_equal_approx(Vector3(2500, 3, -1200)))
	check("and the shift was told", shifts == [Vector3(-3000, 0, 1000)], shifts)

	player.position += Vector3(5000, 0, 0)
	WorldOrigin.follow(player.global_position, false)
	check("past max_distance it shifts however busy", WorldOrigin.offset.x == 8000.0, WorldOrigin.offset)

	var map_transform := WorldOrigin.transform_to_map(player.global_transform)
	check("transforms both ways", WorldOrigin.transform_to_global(map_transform).is_equal_approx(player.global_transform))

	WorldOrigin.shift(Vector3(0.4, 0, 1.6))
	check("shifts are whole metres", WorldOrigin.offset == Vector3(8000, 0, -1002), WorldOrigin.offset)

	WorldOrigin.reset()
	check("reset goes back to the map's own frame", WorldOrigin.offset == Vector3.ZERO and world.position == Vector3.ZERO)
	check("with the player where they were on the map", player.global_position.is_equal_approx(Vector3(7500, 3, -1200)), player.global_position)

	# Whole metres are exact in single precision far further out than any world: a shift there loses nothing.
	WorldOrigin.shift(Vector3(-12345000, 0, 9876000))
	check("an offset thousands of kilometres out is exact", WorldOrigin.offset == Vector3(12345000, 0, -9876000))
	WorldOrigin.reset()

	# A new world loading after another was shifted: reset clears the offset and leaves it be.
	WorldOrigin.shift(Vector3(-2000, 0, 0))
	var next_world := Node3D.new()
	root.add_child(next_world)
	WorldOrigin.world = next_world
	WorldOrigin.reset()
	check("a new world is left where it is, with no offset", next_world.position == Vector3.ZERO and WorldOrigin.offset == Vector3.ZERO)
	WorldOrigin.world = null
	next_world.free()
	world.position = Vector3.ZERO

	# Particles in world space under the world start afresh with a shift (Godot can't move them).
	var rain := GPUParticles3D.new()
	rain.local_coords = false
	rain.emitting = true
	world.add_child(rain)
	await process_frame
	WorldOrigin.shift(Vector3(1000, 0, 0))
	check("a shift with world-space particles in the world goes cleanly", rain.emitting and WorldOrigin.offset == Vector3(-1000, 0, 0))
	WorldOrigin.reset()

	await _servers_follow_a_shift()
	await _streaming()
	await _multimesh_collision()
	await _light_pool()
	await _probe_pool()

	print("world test: %s" % ("all checks passed" if failures == 0 else "%d failed" % failures))
	quit(failures)


# A shift moves the servers in bulk: everything ends up where its node is, on the renderer and in physics,
# whether it moved with the world, stayed outside it, is top-level, or moved for real as the world shifted.
func _servers_follow_a_shift() -> void:
	var world := Node3D.new()
	root.add_child(world)
	current_scene = world
	var inside := thing(world, Vector3(10, 0, 0))
	var outside := thing(root, Vector3(30, 0, 0))
	var holder := Node3D.new()
	world.add_child(holder)
	var top := thing(holder, Vector3(50, 0, 0))
	top.top_level = true
	top.global_position = Vector3(50, 0, 0)
	var mover := thing(world, Vector3(70, 0, 0))
	await frames(3)
	check("the scene starts where its nodes are", drawn_at(Vector3(10, 0, 0)) and solid_at(Vector3(10, 0, 0)) and drawn_at(Vector3(30, 0, 0)))

	mover.position.x += 5.0  # moved for real in the same frame as the shift
	WorldOrigin.shift(Vector3(-1000, 0, 0))
	await frames(3)
	await physics_frame
	check("what moved with the world is drawn and solid where it now is", drawn_at(Vector3(-990, 0, 0)) and solid_at(Vector3(-990, 0, 0)) and not drawn_at(Vector3(10, 0, 0), false) and not solid_at(Vector3(10, 0, 0)))
	check("what's outside the world stays put", drawn_at(Vector3(30, 0, 0)) and solid_at(Vector3(30, 0, 0)) and not drawn_at(Vector3(-970, 0, 0), false))
	check("and so does what's top-level", drawn_at(Vector3(50, 0, 0)) and solid_at(Vector3(50, 0, 0)) and not solid_at(Vector3(-950, 0, 0)))
	check("a node that moved as the world shifted is where it went", drawn_at(Vector3(-925, 0, 0)) and solid_at(Vector3(-925, 0, 0)) and not drawn_at(Vector3(-930, 0, 0), false))
	WorldOrigin.reset()
	await frames(3)
	await physics_frame
	check("and back again with a reset", drawn_at(Vector3(10, 0, 0)) and solid_at(Vector3(10, 0, 0)) and drawn_at(Vector3(30, 0, 0)) and drawn_at(Vector3(75, 0, 0)))
	current_scene = null
	world.free()
	outside.free()


## A box drawn and solid, under a parent.
func thing(parent: Node, at: Vector3) -> StaticBody3D:
	var body := StaticBody3D.new()
	body.position = at
	var shape := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = Vector3(2, 2, 2)
	shape.shape = box
	body.add_child(shape)
	var mesh := MeshInstance3D.new()
	var cube := BoxMesh.new()
	cube.size = Vector3(2, 2, 2)
	mesh.mesh = cube
	body.add_child(mesh)
	parent.add_child(body)
	return body


## Whether something's drawn there. Headless, Godot's stand-in renderer gives meshes no bounds and
## culls nothing, so it can't say: run the test in a window for this (the rest still checks physics).
func drawn_at(at: Vector3, expected := true) -> bool:
	if DisplayServer.get_name() == "headless":
		return expected
	return not RenderingServer.instances_cull_aabb(AABB(at - Vector3(0.5, 0.5, 0.5), Vector3.ONE), root.get_world_3d().scenario).is_empty()


func solid_at(at: Vector3) -> bool:
	return not ray_down(at).is_empty()


# WorldStreamer: cells round the observer come and go, far views beyond, and load_around() at once.
func _streaming() -> void:
	var world := Node3D.new()
	root.add_child(world)
	var streamer := WorldStreamer.new()
	streamer.load_radius = 150.0
	streamer.unload_radius = 250.0
	streamer.far_radius = 400.0
	streamer.far_unload_radius = 500.0
	var observer := Node3D.new()
	world.add_child(observer)
	streamer.observer = observer
	var loaded: Array[String] = []
	var unloading: Array[String] = []
	streamer.cell_loaded.connect(func(name: String, _node: Node, far: bool) -> void: loaded.append(name + (" far" if far else "")))
	streamer.cell_unloading.connect(func(name: String, _node: Node, far: bool) -> void: unloading.append(name + (" far" if far else "")))
	streamer.prepare = func(name: String, node: Node, far: bool) -> void: node.set_meta("prepared", [name, far])
	world.add_child(streamer)
	# A 20 by 20 grid of 100 m cells, 0 to 2 km on the map.
	for x in 20:
		for z in 20:
			streamer.add_cell("%d_%d" % [x, z], Rect2(x * 100, z * 100, 100, 100), "res://cells/cell.tscn", "res://cells/far.tscn")
	observer.position = Vector3(250, 0, 250)
	await settled(streamer)
	check("cells near the observer load", streamer.is_cell_loaded("2_2") and streamer.is_cell_loaded("3_3") and streamer.is_cell_loaded("1_2"))
	check("those beyond load_radius don't", not streamer.is_cell_loaded("6_2") and not streamer.is_cell_loaded("0_0"))
	var cell := streamer.get_cell_node("2_2")
	check("a cell's prepared before it joins", cell != null and cell.get_meta("prepared", []) == ["2_2", false])
	check("its nodes are all there, unique names and all", cell.get_child_count() == 3 and cell.get_node_or_null("%Marker") != null)
	check("far views load beyond the detail", loaded.has("6_2 far") and not loaded.has("15_15 far"))
	var shown_far := 0
	for node in streamer.get_children():
		if node.name.begins_with("Far"):
			if node.visible:
				shown_far += 1
	check("a far view shows only where its detail isn't", shown_far > 0)

	observer.position = Vector3(1550, 0, 1550)
	await settled(streamer)
	check("cells left behind go", not streamer.is_cell_loaded("2_2") and unloading.has("2_2") and streamer.get_cell_node("2_2") == null)
	check("and new ones come", streamer.is_cell_loaded("15_15"))
	var nodes_left := streamer.get_children().filter(func(node: Node) -> bool: return node.get_meta("prepared", ["", false])[0] == "2_2")
	check("a cell gone is freed", nodes_left.is_empty())

	streamer.load_around(Vector3(50, 0, 50), 60.0)
	check("load_around() loads at once", streamer.is_cell_loaded("0_0"))

	streamer.remove_cell("15_15")
	check("a cell removed is gone", not streamer.get_cells().has("15_15") and not streamer.is_cell_loaded("15_15"))
	world.free()


# MultiMeshBody3D: each instance solid, built over frames, a hit naming its instance, moving with a shift.
func _multimesh_collision() -> void:
	var world := Node3D.new()
	root.add_child(world)
	current_scene = world
	var boxes := MultiMesh.new()
	boxes.transform_format = MultiMesh.TRANSFORM_3D
	boxes.instance_count = 200
	# Set whole, as a loaded MultiMesh's is: headless, Godot's stand-in renderer keeps nothing set an instance at a time.
	var data := PackedFloat32Array()
	for i in 200:
		data.append_array([1, 0, 0, (i % 20) * 4.0, 0, 1, 0, 0, 0, 0, 1, (i / 20) * 4.0])
	boxes.buffer = data
	var body := MultiMeshBody3D.new()
	body.multimesh = boxes
	var box := BoxShape3D.new()
	box.size = Vector3(1, 1, 1)
	body.shape = box
	world.add_child(body)
	check("collision is built over frames, not all at once", not body.is_built())
	var waited := 0
	while not body.is_built() and waited < 300:
		await physics_frame
		waited += 1
	check("then every instance is solid", body.is_built(), waited)

	await physics_frame
	var hit := ray_down(Vector3(28, 0, 28))  # instance 7 + 7 * 20 = 147
	check("a hit names the instance", hit.get("collider") == body and body.get_instance(hit.get("rid", RID()), hit.get("shape", -1)) == 147, hit)
	check("and misses where there's none", ray_down(Vector3(2, 0, 2)).is_empty())

	# Every MultiMeshBody3D shares one queue and one budget, the one nearest the camera first.
	var camera := Camera3D.new()
	world.add_child(camera)
	camera.position = Vector3(5000, 0, 0)
	ProjectSettings.set_setting("physics/3d/multimesh_collision_budget_ms", 0.001)
	var near := MultiMeshBody3D.new()
	near.position = Vector3(5000, 0, 0)
	var far := MultiMeshBody3D.new()
	far.position = Vector3(9000, 0, 0)
	for each: MultiMeshBody3D in [far, near]:
		each.multimesh = boxes
		each.shape = box
		world.add_child(each)
	var waited_frames := 0
	while not near.is_built() and waited_frames < 30:  # 4 slices each, one a frame between them
		await frames(1)
		waited_frames += 1
	check("one budget for all of them, the nearest the camera first", near.is_built() and not far.is_built(), [near.is_built(), far.is_built()])
	ProjectSettings.set_setting("physics/3d/multimesh_collision_budget_ms", 1.0)
	while not far.is_built():
		await frames(1)

	body.process_mode = Node.PROCESS_MODE_DISABLED
	await physics_frame
	await physics_frame
	check("switched off, it's out of the physics world", ray_down(Vector3(28, 0, 28)).is_empty())
	body.process_mode = Node.PROCESS_MODE_INHERIT
	await physics_frame
	await physics_frame
	check("and back in when switched on", not ray_down(Vector3(28, 0, 28)).is_empty())

	WorldOrigin.shift(Vector3(-1000, 0, 0))
	await physics_frame
	await physics_frame
	check("collision moves with a shift of the world", not ray_down(Vector3(-972, 0, 28)).is_empty() and ray_down(Vector3(28, 0, 28)).is_empty())
	WorldOrigin.reset()
	current_scene = null
	world.free()


# LightPool3D: a few lights on the best of many points, following the observer.
func _light_pool() -> void:
	var world := Node3D.new()
	root.add_child(world)
	var observer := Node3D.new()
	world.add_child(observer)
	var pool := LightPool3D.new()
	pool.count = 3
	pool.observer = observer
	var lamp := SpotLight3D.new()
	lamp.light_energy = 4.0
	pool.add_child(lamp)
	world.add_child(pool)
	for i in 10:
		pool.add_point(Vector3(i * 10.0, 5, 0))
	await settle_lights()
	check("the nearest points get the lights", lit(pool) == [0, 1, 2], lit(pool))
	check("lit, and the template stays hidden", not lamp.visible and pool.get_children(true).filter(func(n: Node) -> bool: return n is Light3D and n.visible).size() == 3)
	var strong := pool.add_point(Vector3(45, 5, 0), 9.0)  # 45 m out, ranked as 15 m
	await settle_lights()
	check("a strong point outranks a nearer weak one", lit(pool).has(strong) and not lit(pool).has(2), lit(pool))
	observer.position = Vector3(90, 0, 0)
	await settle_lights()
	check("the lights follow the observer", lit(pool).has(9) and lit(pool).has(8), lit(pool))
	pool.remove_point(9)
	await settle_lights()
	check("a point removed lets its light go", not lit(pool).has(9), lit(pool))
	pool.set_point_strength(8, 0.0)
	await settle_lights()
	check("a point turned to strength 0 (a shop shutting) loses its light", not lit(pool).has(8), lit(pool))
	pool.set_point_strength(8, 1.0)
	await settle_lights()
	check("and gets it back when it's turned up", lit(pool).has(8), lit(pool))
	var overhead := pool.add_point(Vector3(90, 20, 0), 4.0)  # on a deck above the observer
	pool.reach_above = 10.0
	await settle_lights()
	check("points too far above (a deck overhead) never get a light", not lit(pool).has(overhead), lit(pool))
	pool.remove_point(overhead)
	pool.reach_above = 0.0
	pool.set_point_color(8, Color.RED)
	var on_eight: Array = pool.get_children(true).filter(func(n: Node) -> bool: return n is Light3D and n.visible and n.position.is_equal_approx(Vector3(80, 5, 0)))
	check("a point's colour change shows at once", on_eight.size() == 1 and on_eight[0].light_color.is_equal_approx(Color.RED), on_eight)
	world.free()


# ReflectionProbePool: probes at the nearest sites, placed one a frame, captured again on request.
func _probe_pool() -> void:
	var world := Node3D.new()
	root.add_child(world)
	var observer := Node3D.new()
	world.add_child(observer)
	var pool := ReflectionProbePool.new()
	pool.count = 3
	pool.observer = observer
	var probe := ReflectionProbe.new()
	probe.box_projection = true
	pool.add_child(probe)
	world.add_child(pool)
	for i in 20:
		pool.add_site(Transform3D(Basis(), Vector3(i * 20.0, 5, 0)), Vector3(20, 10, 20), Vector3(0, -3.5, 0))
	await frames(1)
	check("probes are placed one a frame", pool.get_placed_sites().size() <= 1, pool.get_placed_sites())
	await frames(5)
	check("the nearest sites get probes", pool.get_placed_sites() == PackedInt32Array([0, 1, 2]), pool.get_placed_sites())
	var placed := probes(pool)
	check("copies of the template, sized for their site, the template hidden", not probe.visible and placed.size() == 3 and placed[0].size == Vector3(20, 10, 20) and placed[0].box_projection and placed[0].origin_offset == Vector3(0, -3.5, 0))
	observer.position = Vector3(300, 0, 0)
	await create_timer(0.6).timeout  # its next look at what's near
	await frames(5)
	check("probes follow the observer", pool.get_placed_sites() == PackedInt32Array([14, 15, 16]), pool.get_placed_sites())
	var upstairs := pool.add_site(Transform3D(Basis(), Vector3(300, 40, 0)), Vector3(20, 10, 20))
	pool.height_reach = 20.0
	await create_timer(0.6).timeout
	await frames(5)
	check("sites on another level are left out", not pool.get_placed_sites().has(upstairs), pool.get_placed_sites())
	pool.remove_site(upstairs)
	var before := probes(pool).map(func(p: ReflectionProbe) -> int: return p.get_instance_id())
	pool.recapture()
	await frames(1)
	var after_one := probes(pool).map(func(p: ReflectionProbe) -> int: return p.get_instance_id())
	await frames(5)
	var after_all := probes(pool).map(func(p: ReflectionProbe) -> int: return p.get_instance_id())
	check("recapturing redoes them one a frame", after_one.filter(func(id: int) -> bool: return not before.has(id)).size() == 1 and after_all.all(func(id: int) -> bool: return not before.has(id)))
	world.free()


func probes(pool: ReflectionProbePool) -> Array:
	return pool.get_children(true).filter(func(node: Node) -> bool: return node is ReflectionProbe and node.visible and not node.is_queued_for_deletion())


## Until the light pool's next rankings (every quarter second): headless frames come far faster.
func settle_lights() -> void:
	await create_timer(0.6).timeout
	await frames(2)


func lit(pool: LightPool3D) -> Array:
	var points := []
	for i in pool.count:
		if pool.get_lit_point(i) >= 0:
			points.append(pool.get_lit_point(i))
	points.sort()
	return points


func ray_down(at: Vector3) -> Dictionary:
	var space := root.get_world_3d().direct_space_state
	return space.intersect_ray(PhysicsRayQueryParameters3D.create(at + Vector3.UP * 10.0, at - Vector3.UP * 10.0))


func frames(count: int) -> void:
	for i in count:
		await process_frame


## Until the streamer's caught up (in a window it loads in the background, a few nodes a frame).
func settled(streamer: WorldStreamer) -> void:
	await frames(2)
	var started := Time.get_ticks_msec()
	while streamer.is_busy() and Time.get_ticks_msec() - started < 10000:
		await process_frame
	await frames(30)
