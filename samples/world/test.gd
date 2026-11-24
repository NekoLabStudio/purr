# Checks WorldOrigin. Exits non-zero if a check fails:
#   purr --headless --path samples/world -s test.gd
extends SceneTree

var failures := 0
var shifts: Array[Vector3] = []


# Whether a shader global is there. Only the editor and the headless renderer list them: in a game
# window the renderer refuses, so this trusts it.
func has_global(name: StringName) -> bool:
	return DisplayServer.get_name() != "headless" or RenderingServer.global_shader_parameter_get_list().has(name)


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
	await _kept_through_a_shift()
	await _streaming()
	_multimesh_data()
	await _multimesh_collision()
	await _light_pool()
	await _probe_pool()
	await _scatter()
	await _time_of_day()
	await _instance_group()
	await _static_scenery()
	await _weather()
	await _baked_animations()
	await _impostors()
	await _night_sky()
	await _storms_and_snow()
	await _foliage_benders()
	await _ground_marks()
	await _water()
	await _interiors()

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
	var copies := InstanceGroup3D.new()  # server instances of its own, outside the world
	copies.mesh = BoxMesh.new()
	root.add_child(copies)
	copies.position = Vector3(90, 0, 0)
	copies.set_transforms(copies_at([Vector3.ZERO]))
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
	check("and so does what's outside it with instances of its own", drawn_at(Vector3(90, 0, 0)) and not drawn_at(Vector3(-910, 0, 0), false))
	WorldOrigin.reset()
	await frames(3)
	await physics_frame
	check("and back again with a reset", drawn_at(Vector3(10, 0, 0)) and solid_at(Vector3(10, 0, 0)) and drawn_at(Vector3(30, 0, 0)) and drawn_at(Vector3(75, 0, 0)))
	current_scene = null
	world.free()
	outside.free()
	copies.free()


# What nodes keep in global space outside their transforms moves with them: a trail, the world end of
# a joint with one body, and what visibility ranges are measured from.
func _kept_through_a_shift() -> void:
	var world := Node3D.new()
	root.add_child(world)
	current_scene = world
	var trail := Trail3D.new()
	trail.lifetime = 5.0
	world.add_child(trail)
	for i in 10:
		trail.position.x += 0.5
		await process_frame
	var length := trail.get_current_length()

	# A ball hanging from a joint with no second body, which holds it to the world, as a node and on
	# the physics server alone.
	var ball := RigidBody3D.new()
	var shape := CollisionShape3D.new()
	shape.shape = SphereShape3D.new()
	ball.add_child(shape)
	ball.position = Vector3(20, -2, 0)
	world.add_child(ball)
	var pin := PinJoint3D.new()
	pin.position = Vector3(20, 0, 0)
	world.add_child(pin)
	pin.node_a = pin.get_path_to(ball)
	var physics := PhysicsServer3D
	var server_ball := physics.body_create()
	physics.body_set_mode(server_ball, PhysicsServer3D.BODY_MODE_RIGID)
	var server_shape := physics.sphere_shape_create()
	physics.shape_set_data(server_shape, 0.5)
	physics.body_add_shape(server_ball, server_shape)
	physics.body_set_space(server_ball, world.get_world_3d().space)
	physics.body_set_state(server_ball, PhysicsServer3D.BODY_STATE_TRANSFORM, Transform3D(Basis(), Vector3(40, -2, 0)))
	var server_pin := physics.joint_create()
	physics.joint_make_pin(server_pin, server_ball, Vector3(0, 2, 0), RID(), Vector3(40, 0, 0))
	# And one outside the world, which a shift leaves where it is.
	var outside_ball: RigidBody3D = ball.duplicate()
	outside_ball.position = Vector3(60, -2, 0)
	root.add_child(outside_ball)
	var outside_pin := PinJoint3D.new()
	outside_pin.position = Vector3(60, 0, 0)
	root.add_child(outside_pin)
	outside_pin.node_a = outside_pin.get_path_to(outside_ball)
	for i in 30:
		await physics_frame
	var hanging := ball.global_position
	var outside_hanging := outside_ball.global_position

	WorldOrigin.shift(Vector3(-1000, 0, 0))
	await frames(2)
	for i in 30:
		await physics_frame
	if DisplayServer.get_name() != "headless":  # headless, a trail isn't made
		check("a trail is as long after a shift as before", absf(trail.get_current_length() - length) < 1.0, [length, trail.get_current_length()])
	check("a ball held to the world by a joint stays where it hangs", ball.global_position.distance_to(hanging + Vector3(-1000, 0, 0)) < 0.5, ball.global_position)
	check("and one outside the world stays where it was", outside_ball.global_position.distance_to(outside_hanging) < 0.5, outside_ball.global_position)
	# Only a physics engine that shifts its whole space moves what's only on the server (Jolt).
	var server_at: Vector3 = physics.body_get_state(server_ball, PhysicsServer3D.BODY_STATE_TRANSFORM).origin
	check("and one made on the physics server alone", server_at.distance_to(Vector3(-960, -2, 0)) < 0.5, server_at)
	WorldOrigin.reset()
	physics.free_rid(server_pin)
	physics.free_rid(server_ball)
	physics.free_rid(server_shape)
	outside_pin.free()
	outside_ball.free()
	current_scene = null
	world.free()

	if DisplayServer.get_name() != "headless":
		check("what has a visibility range is drawn after a shift", await _ranged_drawn_after_shift())
		check("debug drawing after a shift is drawn where it's asked", await _debug_line_after_shift())


# A box 5 m in front of the camera, drawn up to 50 m away, in a world shifted 1 km; whether it shows.
func _ranged_drawn_after_shift() -> bool:
	var view := SubViewport.new()
	view.size = Vector2i(32, 32)
	view.own_world_3d = true
	view.transparent_bg = true
	view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(view)
	var world := Node3D.new()
	view.add_child(world)
	var camera := Camera3D.new()
	camera.position = Vector3(0, 0, 5)
	world.add_child(camera)
	var box := MeshInstance3D.new()
	box.mesh = BoxMesh.new()
	box.visibility_range_end = 50.0
	world.add_child(box)
	await frames(4)
	WorldOrigin.world = world
	WorldOrigin.shift(Vector3(-1000, 0, 0))
	await frames(4)
	var shown := view.get_texture().get_image().get_pixel(16, 16).a > 0.5
	WorldOrigin.reset()
	WorldOrigin.world = null
	view.queue_free()
	return shown


# A debug line across the middle of the camera's view, drawn after a shift of the world it's in;
# whether it shows (faint over the default sky, it's told from it by any difference).
func _debug_line_after_shift() -> bool:
	var world := Node3D.new()
	root.add_child(world)
	current_scene = world
	var camera := Camera3D.new()
	world.add_child(camera)
	camera.make_current()
	DisplayServer.window_move_to_foreground()  # a window behind others isn't drawn
	for i in 2:  # drawing before the shift too, as a game would have
		DebugDraw.line(Vector3(0, 0, -5), Vector3(1, 0, -5), Color.WHITE)
		await process_frame
	WorldOrigin.shift(Vector3(-1000, 0, 0))
	var ahead := camera.global_position + Vector3(0, 0, -5)
	for i in 3:
		DebugDraw.line(ahead - Vector3(3, 0, 0), ahead + Vector3(3, 0, 0), Color.WHITE)
		await process_frame
	var image := root.get_texture().get_image()
	var sky := image.get_pixel(4, 4).get_luminance()
	var middle := image.get_size() / 2
	var shown := false
	for y in range(-3, 4):
		shown = shown or absf(image.get_pixelv(middle + Vector2i(0, y)).get_luminance() - sky) > 0.02
	WorldOrigin.reset()
	current_scene = null
	world.free()
	return shown


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


# A MultiMesh with no mesh keeps the buffer it was set: reading it from the renderer waits for the
# GPU to finish the frames in flight (Purr's patch 0050). The renderer's copy is changed behind its
# back here, to tell which one a read gets.
func _multimesh_data() -> void:
	var data := PackedFloat32Array([1, 0, 0, 5, 0, 1, 0, 6, 0, 0, 1, 7, 1, 0, 0, 8, 0, 1, 0, 9, 0, 0, 1, 10])
	var renderers := PackedFloat32Array(data)
	renderers[3] = -1.0
	var multimesh := MultiMesh.new()
	multimesh.transform_format = MultiMesh.TRANSFORM_3D
	multimesh.instance_count = 2
	multimesh.buffer = data
	RenderingServer.multimesh_set_buffer(multimesh.get_rid(), renderers)
	check("a MultiMesh with no mesh keeps the buffer it was set", multimesh.buffer == data, multimesh.buffer)
	multimesh.set_instance_transform(1, Transform3D(Basis(), Vector3(8, 9, 10)))
	check("changed an instance at a time, it reads the renderer's", multimesh.buffer == renderers, multimesh.buffer)
	multimesh.buffer = data
	multimesh.instance_count = 2
	RenderingServer.multimesh_set_buffer(multimesh.get_rid(), renderers)
	check("and given a new count", multimesh.buffer == renderers, multimesh.buffer)
	multimesh.buffer = data
	multimesh.mesh = BoxMesh.new()
	RenderingServer.multimesh_set_buffer(multimesh.get_rid(), renderers)
	check("one given a mesh keeps nothing: it's drawn, and the renderer's is the only copy", multimesh.buffer == renderers, multimesh.buffer)
	multimesh.buffer = data
	RenderingServer.multimesh_set_buffer(multimesh.get_rid(), renderers)
	check("even when it's set after the mesh", multimesh.buffer == renderers, multimesh.buffer)


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


func _scatter() -> void:
	var ground := StaticBody3D.new()
	var shape := CollisionShape3D.new()
	shape.shape = BoxShape3D.new()
	shape.shape.size = Vector3(40, 1, 40)
	ground.add_child(shape)
	ground.position = Vector3(0, -0.5, 0)
	ground.collision_layer = 2
	root.add_child(ground)
	var ramp := StaticBody3D.new()
	var ramp_shape := CollisionShape3D.new()
	ramp_shape.shape = BoxShape3D.new()
	ramp_shape.shape.size = Vector3(10, 1, 10)
	ramp.add_child(ramp_shape)
	ramp.position = Vector3(5, 2, 5)
	ramp.rotation_degrees = Vector3(0, 0, 60) # Too steep to land on.
	ramp.collision_layer = 2
	root.add_child(ramp)
	await frames(2)

	var scatter := Scatter3D.new()
	scatter.mesh = BoxMesh.new()
	scatter.size = Vector3(20, 10, 20)
	scatter.density = 0.5
	scatter.surface_mask = 2
	root.add_child(scatter)
	await frames(1)
	var count := scatter.get_count()
	check("Scatter3D made in code scatters when ready", count > 0 and count <= 200, count)
	var on_ground := true
	var upright := true
	for i in count:
		var placed := copy(scatter, i)
		on_ground = on_ground and is_zero_approx(placed.origin.y)
		upright = upright and placed.basis.y.normalized().is_equal_approx(Vector3.UP)
	check("its copies stand on the ground, not the steep ramp", on_ground)
	check("and upright", upright)
	var first := copy(scatter, 0)
	scatter.scatter()
	check("the same seed scatters the same way", copy(scatter, 0).is_equal_approx(first))
	scatter.seed = 1
	scatter.scatter()
	check("another seed scatters another way", not copy(scatter, 0).is_equal_approx(first))
	scatter.max_slope = 90.0
	scatter.scatter()
	check("a steeper limit lets copies onto the ramp", scatter.get_count() > count, [scatter.get_count(), count])
	scatter.surface_mask = 4
	scatter.scatter()
	check("with nothing below on its mask, nothing's placed", scatter.get_count() == 0, scatter.get_count())

	scatter.surface_mask = 2
	scatter.max_slope = 45.0
	scatter.collision_shape = BoxShape3D.new()
	scatter.collision_layer = 8
	scatter.scatter()
	await frames(5)
	var spot := scatter.global_transform * copy(scatter, 0).origin
	var space := root.get_world_3d().direct_space_state
	var query := PhysicsRayQueryParameters3D.create(spot + Vector3.UP * 5.0, spot - Vector3.UP)
	query.collision_mask = 8
	check("a collision shape makes the copies solid", not space.intersect_ray(query).is_empty())
	scatter.queue_free()
	ground.queue_free()
	ramp.queue_free()
	await frames(1)


func _time_of_day() -> void:
	var environment := WorldEnvironment.new()
	environment.environment = Environment.new()
	environment.environment.background_energy_multiplier = 1.0
	root.add_child(environment)
	var day := TimeOfDay.new()
	var sun := DirectionalLight3D.new()
	var moon := DirectionalLight3D.new()
	day.add_child(sun)
	day.add_child(moon)
	day.day_length = 0.0
	root.add_child(day)
	await frames(1)

	day.time = 12.0
	check("at noon the sun is high and to the south", day.get_sun_direction().y > 0.6 and day.get_sun_direction().z > 0.3, day.get_sun_direction())
	check("and shines down", (-sun.global_basis.z).dot(Vector3.DOWN) > 0.6, -sun.global_basis.z)
	check("the moon is down and off", not moon.visible)
	day.time = 6.5
	check("in the morning the sun is low in the east", day.get_sun_direction().x > 0.9 and day.get_sun_direction().y > 0.0, day.get_sun_direction())
	check("and warmer", sun.light_color.b < 0.6, sun.light_color)
	day.time = 0.0
	check("at midnight it's night", not day.is_day() and not sun.visible and moon.visible)
	check("and the sky dims", environment.environment.background_energy_multiplier < 0.2, environment.environment.background_energy_multiplier)
	day.time = 12.0
	check("and comes back by day", is_equal_approx(environment.environment.background_energy_multiplier, 1.0))

	day.rotation_degrees.y = 90.0
	check("turning it turns the compass", day.get_sun_direction().x > 0.3, day.get_sun_direction())
	day.rotation_degrees.y = 0.0

	var hours: Array[int] = []
	day.hour_changed.connect(func(hour: int) -> void: hours.append(hour))
	day.time = 12.99
	day.day_length = 1.0  # A day a second: an hour in 0.04 s.
	await create_timer(0.2).timeout
	check("the clock runs and says when an hour passes", day.time > 13.0 and hours.size() > 0 and hours[0] == 13, [day.time, hours])
	hours.clear()
	day.day_length = 0.1  # Several hours a frame.
	await frames(3)
	day.day_length = 0.0
	var in_order := hours.size() > 3
	for i in range(1, hours.size()):
		in_order = in_order and hours[i] == (hours[i - 1] + 1) % 24
	check("an hour isn't skipped when a frame spans several", in_order, hours)
	day.day_length = 0.0
	day.time = 0.0
	root.remove_child(day)
	check("taken away at night, it leaves the sky as bright as it was", is_equal_approx(environment.environment.background_energy_multiplier, 1.0), environment.environment.background_energy_multiplier)
	root.add_child(day)
	day.queue_free()
	environment.queue_free()
	await frames(1)


## A scattered copy's transform, read from the buffer (headless, Godot's stand-in renderer only keeps it whole).
func copy(scatter: Scatter3D, index: int) -> Transform3D:
	var b := scatter.multimesh.buffer.slice(index * 12, index * 12 + 12)
	return Transform3D(Vector3(b[0], b[4], b[8]), Vector3(b[1], b[5], b[9]), Vector3(b[2], b[6], b[10]), Vector3(b[3], b[7], b[11]))


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


# InstanceGroup3D: copies in chunks, as many as the cells they're in, made and freed as they move.
func copies_at(positions: Array) -> PackedFloat32Array:
	var floats := PackedFloat32Array()
	for at: Vector3 in positions:
		floats.append_array([1, 0, 0, at.x, 0, 1, 0, at.y, 0, 0, 1, at.z])
	return floats


func _instance_group() -> void:
	var group := InstanceGroup3D.new()
	group.mesh = BoxMesh.new()
	group.chunk_size = 10.0
	root.add_child(group)
	var spread := []
	for i in 100:
		spread.append(Vector3(i % 2 * 10 + 1, 0, i / 50 * 10 + 1))
	group.set_transforms(copies_at(spread))
	check("a copy's transform is 12 floats", group.get_count() == 100, group.get_count())
	await process_frame
	check("copies are drawn in a chunk for each cell they're in", group.get_chunk_count() == 4, group.get_chunk_count())
	check("and drawn where they are", drawn_at(Vector3(11, 0, 11)))
	var together := []
	for i in 100:
		together.append(Vector3(1 + i * 0.05, 0, 1))
	group.set_transforms(copies_at(together))
	group.set_transforms(copies_at(together))
	await process_frame
	check("chunks nothing is in go", group.get_chunk_count() == 1, group.get_chunk_count())
	group.set_transforms(copies_at([Vector3(101, 0, 101), Vector3(102, 0, 101)]))
	await process_frame
	check("a chunk's box is round its copies, however much room it has", drawn_at(Vector3(101, 0, 101)) and not drawn_at(Vector3(50, 0, 50), false))
	group.set_transforms(copies_at(together))
	group.set_transforms(PackedFloat32Array([1, 2, 3]))
	check("a buffer that isn't whole copies is refused", group.get_count() == 100, group.get_count())
	group.use_colors = true
	group.set_colors(PackedColorArray([Color.RED]))
	await process_frame
	check("colours can be added, and copies without one are white", group.get_chunk_count() == 1)
	root.remove_child(group)
	check("leaving the scene frees the chunks", group.get_chunk_count() == 0)
	root.add_child(group)
	await process_frame
	check("and coming back makes them again", group.get_chunk_count() == 1, group.get_chunk_count())
	group.set_transforms(PackedFloat32Array())
	await process_frame
	check("no copies, no chunks", group.get_chunk_count() == 0 and group.get_count() == 0)
	group.queue_free()

	var late := InstanceGroup3D.new()
	root.add_child(late)
	late.set_transforms(copies_at([Vector3(201, 0, 201)]))
	await process_frame
	late.mesh = BoxMesh.new()
	await process_frame
	check("a mesh given after the copies is drawn where they are", drawn_at(Vector3(201, 0, 201)))
	late.queue_free()

	if DisplayServer.get_name() != "headless":
		check("a camera that doesn't see the copies' layer doesn't draw them", not await _layer_drawn(1))
		check("one that does, does", await _layer_drawn(2))


# Draws an InstanceGroup3D on layer 2 with a camera seeing only cull_mask; whether anything shows.
func _layer_drawn(cull_mask: int) -> bool:
	var view := SubViewport.new()
	view.size = Vector2i(32, 32)
	view.own_world_3d = true
	view.transparent_bg = true
	view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(view)
	var camera := Camera3D.new()
	camera.position = Vector3(0, 0, 5)
	camera.cull_mask = cull_mask
	view.add_child(camera)
	var group := InstanceGroup3D.new()
	group.mesh = BoxMesh.new()
	group.layers = 2
	view.add_child(group)
	group.set_transforms(copies_at([Vector3.ZERO]))
	for i in 4:
		await process_frame
	var shown := view.get_texture().get_image().get_pixel(16, 16).a > 0.5
	view.queue_free()
	return shown


# Static scenery: boxes sharing a material, and a MultiMesh of another, become one mesh each.
func _static_scenery() -> void:
	var scenery := StaticScenery3D.new()
	var stone := StandardMaterial3D.new()
	var wood := StandardMaterial3D.new()
	var glass := StandardMaterial3D.new()
	glass.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	var box := BoxMesh.new()
	var boxes := 20
	for i in boxes:
		var piece := MeshInstance3D.new()
		piece.mesh = box
		piece.material_override = stone
		piece.position = Vector3(i * 2.0, 0, 0)
		piece.rotation.y = i * 0.3
		scenery.add_child(piece)
	var fence := MultiMeshInstance3D.new()
	fence.multimesh = MultiMesh.new()
	fence.multimesh.transform_format = MultiMesh.TRANSFORM_3D
	var post := CylinderMesh.new()
	post.material = wood
	fence.multimesh.mesh = post
	fence.multimesh.instance_count = 10
	for i in 10:
		fence.multimesh.set_instance_transform(i, Transform3D(Basis(), Vector3(i, 0, 5)))
	scenery.add_child(fence)
	var window := MeshInstance3D.new()
	window.mesh = QuadMesh.new()
	window.material_override = glass
	scenery.add_child(window)
	root.add_child(scenery)
	await scenery.merged
	var meshes: Array[ArrayMesh] = scenery.get_merged_meshes()
	check("static scenery merges each material's pieces into one mesh", scenery.is_merged() and meshes.size() == 2, meshes.size())
	var by_material := {}
	for mesh in meshes:
		by_material[mesh.surface_get_material(0)] = mesh
	var box_vertices: int = box.get_mesh_arrays()[Mesh.ARRAY_VERTEX].size()
	var post_vertices: int = post.get_mesh_arrays()[Mesh.ARRAY_VERTEX].size()
	check("with every piece in it", by_material.has(stone) and by_material[stone].surface_get_array_len(0) == box_vertices * boxes, by_material.get(stone))
	check("and every copy of a MultiMesh", by_material.has(wood) and by_material[wood].surface_get_array_len(0) == post_vertices * 10, by_material.get(wood))
	check("but not what's see-through, which draws in its own order", not by_material.has(glass))
	var last: Vector3 = by_material[stone].surface_get_arrays(0)[Mesh.ARRAY_VERTEX][box_vertices * boxes - 1]
	check("each piece where it stood", last.distance_to(Vector3((boxes - 1) * 2.0, 0, 0)) < 1.0, last)

	var late := MeshInstance3D.new()
	late.mesh = box
	late.material_override = stone
	scenery.add_child(late)
	scenery.merge()
	await scenery.merged
	meshes = scenery.get_merged_meshes()
	var stones := meshes.filter(func(mesh: ArrayMesh) -> bool: return mesh.surface_get_material(0) == stone)
	check("merging again takes in what's been added", stones.size() == 1 and stones[0].surface_get_array_len(0) == box_vertices * (boxes + 1))

	scenery.chunk_size = 10.0
	scenery.merge()
	await scenery.merged
	check("pieces far apart are merged by chunk, so each chunk is culled on its own", scenery.get_merged_meshes().size() > 2, scenery.get_merged_meshes().size())
	scenery.queue_free()

	var meadow := StaticScenery3D.new()
	var grass := MultiMeshInstance3D.new()
	grass.multimesh = MultiMesh.new()
	grass.multimesh.transform_format = MultiMesh.TRANSFORM_3D
	grass.multimesh.mesh = BoxMesh.new()
	grass.multimesh.instance_count = 5000
	meadow.add_child(grass)
	var stone_wall := MeshInstance3D.new()
	stone_wall.mesh = BoxMesh.new()
	meadow.add_child(stone_wall)
	root.add_child(meadow)
	await meadow.merged
	check("a MultiMesh too big to merge (a meadow's grass) draws instanced as it was", meadow.get_merged_meshes().size() == 1 and meadow.get_merged_meshes()[0].surface_get_array_len(0) == box_vertices, meadow.get_merged_meshes().size())
	meadow.queue_free()

	var hidden := StaticScenery3D.new()
	hidden.visible = false
	var piece := MeshInstance3D.new()
	piece.mesh = box
	hidden.add_child(piece)
	root.add_child(hidden)
	await hidden.merged
	check("scenery hidden as it starts merges all the same, for when it's shown", hidden.get_merged_meshes().size() == 1, hidden.get_merged_meshes().size())
	hidden.queue_free()


# Weather: clouds and rain ease towards what's asked, the ground wets and dries, the sun dims.
func _weather() -> void:
	var holder := Node3D.new()
	root.add_child(holder)
	var day := TimeOfDay.new()
	day.time = 12.0
	day.day_length = 0.0
	var sun := DirectionalLight3D.new()
	day.add_child(sun)
	holder.add_child(day)
	var weather := Weather3D.new()
	weather.cloud_cover = 0.2
	holder.add_child(weather)
	await process_frame
	var clear_sun := sun.light_energy
	check("weather starts as set", is_equal_approx(weather.get_current_cloud_cover(), 0.2) and weather.get_current_rain() == 0.0)
	check("and publishes it for shaders", has_global(&"weather_wetness") and has_global(&"time_of_day_sun_direction"))

	weather.change_time = 0.3
	weather.wetting_time = 0.3
	weather.drying_time = 3.0
	weather.rain = 1.0
	var started := Time.get_ticks_msec()
	while weather.get_current_rain() < 1.0 and Time.get_ticks_msec() - started < 3000:
		await process_frame
	check("rain brings cloud over the sun before it falls", weather.get_current_cloud_cover() > 0.94 and weather.get_overcast() > 0.6 and weather.get_current_rain() == 1.0, [weather.get_current_cloud_cover(), weather.get_overcast(), weather.get_current_rain()])
	check("which dims the sun", sun.light_energy < clear_sun * 0.5, [sun.light_energy, clear_sun])
	while weather.get_wetness() < 1.0 and Time.get_ticks_msec() - started < 4000:
		await process_frame
	check("the ground gets wet", is_equal_approx(weather.get_wetness(), 1.0), weather.get_wetness())

	weather.rain = 0.0
	started = Time.get_ticks_msec()
	while weather.get_current_rain() > 0.0 and Time.get_ticks_msec() - started < 3000:
		await process_frame
	check("when the rain stops, the ground is still wet a while", weather.get_wetness() > 0.6, weather.get_wetness())

	weather.cloud_cover = 0.1
	weather.settle()
	check("settle() jumps to the weather asked for, dry", is_equal_approx(weather.get_current_cloud_cover(), 0.1) and weather.get_wetness() == 0.0 and weather.get_overcast() == 0.0)
	var sky := CloudySkyMaterial.new()
	sky.cloud_cover = 1.5
	check("a cloudy sky's cover stays within 0 and 1", sky.cloud_cover == 1.0)
	check("and it has its own material on the renderer, for a Sky to draw", sky.get_rid().is_valid())
	var air := AtmosphereSkyMaterial.new()
	air.haze = -1.0
	check("the air's sky has its material, and no less than no haze", air.get_rid().is_valid() and air.haze == 0.0)
	var grass := FoliageMaterial.new()
	grass.alpha_scissor_threshold = 2.0
	check("foliage's cutout stays within 0 and 1, and it has its material", grass.alpha_scissor_threshold == 1.0 and grass.get_rid().is_valid())
	check("the wind is there for foliage to sway in", has_global(&"weather_wind"))
	holder.queue_free()


# A skinned character's animation baked into textures, for crowds drawn on the GPU.
func _baked_animations() -> void:
	var character := Node3D.new()
	var skeleton := Skeleton3D.new()
	skeleton.name = "Skeleton3D"
	skeleton.add_bone("root")
	skeleton.add_bone("top")
	skeleton.set_bone_parent(1, 0)
	skeleton.set_bone_rest(1, Transform3D(Basis(), Vector3(0, 1, 0)))
	skeleton.reset_bone_poses()
	character.add_child(skeleton)
	# A strip: its bottom on the root bone, its top on the top bone.
	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3(-0.1, 0, 0), Vector3(0.1, 0, 0), Vector3(-0.1, 2, 0), Vector3(0.1, 2, 0)])
	arrays[Mesh.ARRAY_NORMAL] = PackedVector3Array([Vector3.BACK, Vector3.BACK, Vector3.BACK, Vector3.BACK])
	arrays[Mesh.ARRAY_BONES] = PackedInt32Array([0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0])
	arrays[Mesh.ARRAY_WEIGHTS] = PackedFloat32Array([1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0])
	arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0, 2, 1, 1, 2, 3])
	var strip := ArrayMesh.new()
	strip.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var skin := Skin.new()
	skin.add_bind(0, Transform3D())
	skin.add_bind(1, Transform3D(Basis(), Vector3(0, -1, 0)))
	var body := MeshInstance3D.new()
	body.mesh = strip
	body.skin = skin
	skeleton.add_child(body)
	body.skeleton = NodePath("..")
	var player := AnimationPlayer.new()
	character.add_child(player)
	var bend := Animation.new()
	bend.length = 1.0
	bend.loop_mode = Animation.LOOP_LINEAR
	var track := bend.add_track(Animation.TYPE_ROTATION_3D)
	bend.track_set_path(track, "Skeleton3D:top")
	bend.rotation_track_insert_key(track, 0.0, Quaternion())
	bend.rotation_track_insert_key(track, 0.5, Quaternion(Vector3.BACK, PI / 2))
	bend.rotation_track_insert_key(track, 1.0, Quaternion())
	var library := AnimationLibrary.new()
	library.add_animation("bend", bend)
	player.add_animation_library("", library)
	root.add_child(character)
	await process_frame

	var baked := BakedAnimations.new()
	check("a skinned character's animations can be baked", baked.bake(character, player, PackedStringArray(), 10.0) == OK)
	check("each animation's frames are kept, a looping one without its last", baked.get_animation_names() == PackedStringArray(["bend"]) and is_equal_approx(baked.get_length("bend"), 1.0), baked.get_animations())
	check("a copy's custom data says the animation and where in it", baked.get_custom_data("bend", 1.25) == Color(0, 10, 2.5, 1), baked.get_custom_data("bend", 1.25))
	var image := baked.positions.get_image()
	var top_at := func(frame: int) -> Vector3:
		var pixel := image.get_pixel(2, frame * baked.rows_per_frame)
		return Vector3(pixel.r, pixel.g, pixel.b)
	var bottom_at := func(frame: int) -> Vector3:
		var pixel := image.get_pixel(0, frame * baked.rows_per_frame)
		return Vector3(pixel.r, pixel.g, pixel.b)
	check("the top of the strip moves as the bone bends", top_at.call(0).distance_to(Vector3(-0.1, 2, 0)) < 0.01 and top_at.call(5).distance_to(Vector3(-1, 0.9, 0)) < 0.01, [top_at.call(0), top_at.call(5)])
	check("its bottom stays put", bottom_at.call(5).distance_to(Vector3(-0.1, 0, 0)) < 0.01, bottom_at.call(5))
	check("the mesh to draw has no bones, and bounds every frame", baked.mesh.get_surface_count() == 1 and baked.mesh.get_custom_aabb().has_point(Vector3(-0.9, 1.0, 0)), baked.mesh.get_custom_aabb())
	var material := BakedAnimationMaterial.new()
	material.baked_animations = baked
	check("its material draws it", material.get_rid().is_valid())
	character.queue_free()


# Distant trees as impostors: a quad showing the tree as baked from the nearest direction.
func _impostors() -> void:
	var tree := MeshInstance3D.new()
	tree.mesh = CylinderMesh.new()
	root.add_child(tree)
	var impostor := Impostor.new()
	if DisplayServer.get_name() == "headless":
		var refused: Dictionary = await impostor.bake(tree)
		check("impostors are baked by rendering, so not headless", refused.ok == false and refused.error == "no_renderer", refused)
	else:
		var baked: Dictionary = await impostor.bake(tree, 4, 32)
		check("a node bakes into an impostor", baked.ok and impostor.texture != null and impostor.texture.get_width() == 128, baked)
	tree.queue_free()
	impostor.texture = ImageTexture.create_from_image(Image.create_empty(64, 64, false, Image.FORMAT_RGBA8))
	impostor.radius = 2.0
	var quad := impostor.get_mesh()
	check("an impostor is drawn as a quad of its own material", quad is QuadMesh and quad.material is ImpostorMaterial and quad.material.get_rid().is_valid() and quad.get_custom_aabb().size.x == 4.0, quad)

	var ground := StaticBody3D.new()
	var floor_shape := CollisionShape3D.new()
	floor_shape.shape = WorldBoundaryShape3D.new()
	ground.add_child(floor_shape)
	root.add_child(ground)
	var scatter := Scatter3D.new()
	scatter.mesh = CylinderMesh.new()
	scatter.density = 0.5
	scatter.impostor = impostor
	scatter.impostor_distance = 80.0
	root.add_child(scatter)
	await physics_frame
	scatter.scatter()
	var far: MultiMeshInstance3D = scatter.get_children(true).filter(func(child: Node) -> bool: return child is MultiMeshInstance3D)[0] if scatter.get_children(true).any(func(child: Node) -> bool: return child is MultiMeshInstance3D) else null
	check("a scatter with an impostor draws it past its distance", far != null and far.multimesh.instance_count == scatter.get_count() and far.visibility_range_begin < 80.0 and scatter.visibility_range_end > 80.0, [far, scatter.visibility_range_end])
	scatter.impostor = null
	check("and stops when it has none", scatter.visibility_range_end == 0.0)
	scatter.queue_free()
	ground.queue_free()


# The moon in its phases and the stars turning, and lights that come on at dusk.
func _night_sky() -> void:
	var holder := Node3D.new()
	root.add_child(holder)
	var day := TimeOfDay.new()
	day.day_length = 0.0
	day.time = 0.0
	day.moon_phase = 0.5
	var sun := DirectionalLight3D.new()
	var moon := DirectionalLight3D.new()
	day.add_child(sun)
	day.add_child(moon)
	holder.add_child(day)
	var lamp := OmniLight3D.new()
	lamp.add_to_group(&"night_lights")
	holder.add_child(lamp)
	var other_lamp := OmniLight3D.new()
	other_lamp.position = Vector3(12, 0, 3)
	other_lamp.add_to_group(&"night_lights")
	holder.add_child(other_lamp)
	await process_frame
	check("a full moon is opposite the sun", day.get_moon_direction().is_equal_approx(-day.get_sun_direction()), [day.get_moon_direction(), day.get_sun_direction()])
	check("and lights the night", moon.visible and moon.light_energy > 0.0 and is_equal_approx(day.get_moon_light(), 1.0))
	check("at midnight the night lights are on", lamp.visible and other_lamp.visible)
	check("the moon, the stars and the night are published for shaders", has_global(&"time_of_day_night") and has_global(&"time_of_day_moon_direction") and has_global(&"time_of_day_star_rotation"))
	day.moon_phase = 0.0
	check("a new moon is beside the sun and gives no light", day.get_moon_direction().is_equal_approx(day.get_sun_direction()) and not moon.visible and day.get_moon_light() < 0.001)
	var midnight_stars: Basis = day.get_star_rotation()
	var pole := Vector3(0, sin(deg_to_rad(day.latitude)), -cos(deg_to_rad(day.latitude)))
	day.time = 6.0
	var dawn_stars: Basis = day.get_star_rotation()
	check("the stars turn with the hours", not midnight_stars.is_equal_approx(dawn_stars))
	check("round the celestial pole", (dawn_stars * pole).is_equal_approx(pole) and (midnight_stars * pole).is_equal_approx(pole), dawn_stars * pole)
	day.time = 12.0
	check("at noon the night lights are off", not lamp.visible and not other_lamp.visible)
	var lit_at := {}
	for step in 60:
		day.time = 17.0 + step * 0.05
		for each in [lamp, other_lamp]:
			if each.visible and not lit_at.has(each):
				lit_at[each] = day.time
	check("at dusk they come on, each at its own moment", lit_at.size() == 2 and lit_at[lamp] != lit_at[other_lamp], lit_at)
	day.day_length = 2400.0
	var phase_before := day.moon_phase
	for i in 10:
		await process_frame
	check("the moon's phase moves on as the days pass", day.moon_phase > phase_before, [phase_before, day.moon_phase])
	holder.queue_free()
	await process_frame


# Snow that settles and melts, fog that thickens the world's, and storms' lightning.
func _storms_and_snow() -> void:
	var holder := Node3D.new()
	root.add_child(holder)
	var environment := Environment.new()
	environment.fog_enabled = false
	var world_environment := WorldEnvironment.new()
	world_environment.environment = environment
	holder.add_child(world_environment)
	var weather := Weather3D.new()
	weather.change_time = 0.2
	weather.settling_time = 0.3
	weather.melting_time = 0.6
	holder.add_child(weather)
	await process_frame
	weather.snow = 1.0
	var started := Time.get_ticks_msec()
	while weather.get_snow_cover() < 1.0 and Time.get_ticks_msec() - started < 4000:
		await process_frame
	check("snow falls once it's overcast, and settles", is_equal_approx(weather.get_current_snow(), 1.0) and is_equal_approx(weather.get_snow_cover(), 1.0), [weather.get_current_snow(), weather.get_snow_cover()])
	check("snow brings fog of its own into the world's", environment.fog_enabled and environment.fog_density > 0.0, environment.fog_density)
	check("it's published for shaders", has_global(&"weather_snow_cover"))
	weather.snow = 0.0
	started = Time.get_ticks_msec()
	while weather.get_current_snow() > 0.0 and Time.get_ticks_msec() - started < 3000:
		await process_frame
	check("when it stops, the snow lies a while", weather.get_snow_cover() > 0.3, weather.get_snow_cover())

	var distances: Array[float] = []
	weather.lightning_flashed.connect(func(distance: float) -> void: distances.append(distance))
	weather.flash()
	await process_frame
	check("lightning flashes, saying how far away for the thunder", distances.size() == 1 and distances[0] >= 400.0 and distances[0] <= 6000.0, distances)
	var flash: DirectionalLight3D = null
	for child in weather.get_children(true):
		if child is DirectionalLight3D:
			flash = child
	check("and lights the world", flash != null and flash.visible and flash.light_energy > 0.0)
	await create_timer(1.2).timeout
	check("briefly", not flash.visible)

	weather.lightning = 1.0
	weather.rain = 1.0
	distances.clear()
	started = Time.get_ticks_msec()
	while distances.is_empty() and Time.get_ticks_msec() - started < 15000:
		await process_frame
	check("a storm brings lightning by itself", not distances.is_empty())
	holder.remove_child(weather)
	check("leaving, it gives the world its own fog back", not environment.fog_enabled and is_equal_approx(environment.fog_density, 0.01), [environment.fog_enabled, environment.fog_density])
	weather.free()
	holder.queue_free()
	await process_frame


# Grass bending away from what moves through it.
func _foliage_benders() -> void:
	var bender := FoliageBender3D.new()
	bender.radius = 1.5
	root.add_child(bender)
	bender.global_position = Vector3(3, 0, -2)
	await process_frame
	check("benders are a shader global for foliage", has_global(&"foliage_benders"))
	check("their reach stays above nothing", (func() -> bool: bender.radius = -1.0; return bender.radius > 0.0).call())
	bender.queue_free()
	await process_frame
	var grass := MeshInstance3D.new()
	grass.mesh = QuadMesh.new()
	grass.material_override = FoliageMaterial.new()
	root.add_child(grass)
	await process_frame
	check("foliage draws with benders and snow without errors", grass.material_override.get_rid().is_valid())
	grass.queue_free()


# Marks left on the ground: strips and stamps that fade.
func _ground_marks() -> void:
	var ground := StaticBody3D.new()
	var shape := CollisionShape3D.new()
	shape.shape = BoxShape3D.new()
	shape.shape.size = Vector3(100, 1, 100)
	ground.add_child(shape)
	ground.position = Vector3(0, -0.5, 0)
	root.add_child(ground)
	var marks := GroundMarks3D.new()
	marks.spacing = 0.5
	marks.lifetime = 0.6
	root.add_child(marks)
	marks.global_position = Vector3(0, 0.3, 0)
	await physics_frame
	await physics_frame
	marks.emitting = true
	for i in 10:
		marks.global_position += Vector3(0.6, 0, 0)
		await process_frame
	check("GroundMarks3D lays a mark every spacing while it moves", marks.get_mark_count() >= 9, marks.get_mark_count())
	marks.emitting = false
	var count := marks.get_mark_count()
	marks.global_position += Vector3(3, 0, 0)
	await process_frame
	check("and none when it's not emitting", marks.get_mark_count() <= count)
	marks.emitting = true
	marks.global_position = Vector3(0, 30, 0)
	await process_frame
	check("nor off the ground", marks.get_mark_count() <= count)
	marks.emitting = false
	await create_timer(0.8).timeout
	await process_frame
	check("marks fade away after their lifetime", marks.get_mark_count() == 0, marks.get_mark_count())
	marks.mode = GroundMarks3D.MODE_STAMPS
	marks.global_position = Vector3(0, 0.3, 0)
	marks.emitting = true
	for i in 4:
		marks.global_position += Vector3(0, 0, 0.6)
		await process_frame
	check("stamps too", marks.get_mark_count() >= 3, marks.get_mark_count())
	var before := marks.get_mark_count()
	WorldOrigin.shift(Vector3(-1000, 0, 0))
	check("marks stay put when the world shifts", marks.get_mark_count() == before)
	WorldOrigin.reset()
	marks.clear()
	check("clear() takes them all away", marks.get_mark_count() == 0)
	marks.queue_free()
	ground.queue_free()
	await process_frame


# Water: a lake or an endless sea, and how high its waves are, for things that float.
func _water() -> void:
	var sea := Water3D.new()
	sea.position = Vector3(0, 2, 0)
	root.add_child(sea)
	await process_frame
	var material := WaterMaterial.new()
	material.wave_height = 1.0
	material.follow_wind = false
	sea.material = material
	var highest := 0.0
	var differs := false
	var first := sea.get_height_at(Vector3(0, 0, 0))
	for x in 20:
		var height := sea.get_height_at(Vector3(x * 1.7, 0, x * 0.9))
		highest = maxf(highest, absf(height - 2.0))
		differs = differs or not is_equal_approx(height, first)
	check("the sea's surface rises and falls about its own height", differs and highest <= 1.0, highest)
	material.wave_height = 0.0
	check("still water is flat", is_equal_approx(sea.get_height_at(Vector3(5, 0, 5)), 2.0))
	material.choppiness = 3.0
	check("choppiness stays within 0 and 1", material.choppiness == 1.0)
	material.wave_height = 0.5
	WorldOrigin.shift(Vector3(-1000, 0, 0))
	var after := sea.get_height_at(Vector3(-1000 + 5, 0, 5))
	WorldOrigin.reset()
	check("waves stay put when the world shifts", is_equal_approx(after, sea.get_height_at(Vector3(5, 0, 5))), [after, sea.get_height_at(Vector3(5, 0, 5))])
	var lake := Water3D.new()
	lake.size = Vector2(30, 20)
	root.add_child(lake)
	await process_frame
	check("a lake and the sea draw without errors", material.get_rid().is_valid())
	lake.queue_free()
	sea.queue_free()


# Windows with rooms behind them.
func _interiors() -> void:
	var front := MeshInstance3D.new()
	front.mesh = QuadMesh.new()
	var rooms := InteriorMaterial.new()
	rooms.rooms = Vector2i(0, 4)
	front.material_override = rooms
	root.add_child(front)
	await process_frame
	check("a building's windows have at least one room each way", rooms.rooms == Vector2i(1, 4))
	check("and draw without errors", rooms.get_rid().is_valid())
	front.queue_free()
