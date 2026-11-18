# Checks VirtualCamera3D and VirtualCamera2D. Exits non-zero if a check fails:
#   purr --headless --path samples/cameras -s test.gd
extends SceneTree

var failures := 0


func check(what: String, ok: bool, detail: Variant = null) -> void:
	print("check: ", what, "" if ok else "  FAILED  %s" % [detail])
	if not ok:
		failures += 1


func frames(count: int) -> void:
	for i in count:
		await process_frame


func physics_frames(count: int) -> void:
	for i in count:
		await physics_frame


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	await check_3d()
	await check_2d()
	print("cameras: %d failed" % failures if failures else "cameras: all passed")
	quit(1 if failures else 0)


func virtual_camera(parent: Node, priority := 0) -> VirtualCamera3D:
	var camera := VirtualCamera3D.new()
	camera.priority = priority
	camera.blend_time = 0.0
	parent.add_child(camera)
	return camera


func check_3d() -> void:
	var world := Node3D.new()
	root.add_child(world)
	var camera := Camera3D.new()
	world.add_child(camera)
	var target := Node3D.new()
	target.position = Vector3(10, 0, 0)
	world.add_child(target)

	var low := virtual_camera(world, 0)
	low.position = Vector3(0, 5, 0)
	var high := virtual_camera(world, 5)
	high.position = Vector3(0, 0, 9)
	high.fov = 50.0
	await frames(2)
	check("the real camera takes the highest priority's place", camera.global_position.is_equal_approx(Vector3(0, 0, 9)), camera.global_position)
	check("and its field of view", is_equal_approx(camera.fov, 50.0), camera.fov)
	check("which is live", high.is_live() and not low.is_live())

	var changes: Array[bool] = []
	low.live_changed.connect(changes.append)
	low.priority = 5
	await frames(1)
	check("on a tie, the one ranked last wins", camera.global_position.is_equal_approx(Vector3(0, 5, 0)), camera.global_position)
	check("and hears it's live", changes == [true], changes)

	low.process_mode = Node.PROCESS_MODE_DISABLED
	await frames(1)
	check("a camera that can't process doesn't compete", high.is_live() and camera.global_position.is_equal_approx(Vector3(0, 0, 9)), camera.global_position)
	high.process_mode = Node.PROCESS_MODE_DISABLED
	await frames(1)
	check("when none can compete, none is live", not high.is_live() and not low.is_live())
	high.process_mode = Node.PROCESS_MODE_INHERIT
	await frames(1)
	check("and the next to compete is live again", high.is_live())
	low.process_mode = Node.PROCESS_MODE_INHERIT
	low.priority = 0

	# Blending: from where the real camera was, over the new camera's blend time.
	await frames(1)
	high.priority = 0
	low.priority = 9
	low.blend_time = 0.5
	low.blend_transition = Tween.TRANS_LINEAR
	await create_timer(0.25).timeout
	await frames(1)
	var halfway := camera.global_position
	check("a blend goes part of the way", halfway.y > 1.0 and halfway.y < 4.0 and halfway.z > 3.0 and halfway.z < 8.0, halfway)
	await create_timer(0.4).timeout
	await frames(1)
	check("and arrives", camera.global_position.is_equal_approx(Vector3(0, 5, 0)), camera.global_position)
	low.blend_time = 0.0

	var follower := virtual_camera(world, 20)
	follower.follow_mode = VirtualCamera3D.FOLLOW_TARGET
	follower.follow_target = follower.get_path_to(target)
	follower.follow_offset = Vector3(0, 2, 3)
	await frames(1)
	check("following a target keeps the offset from it", camera.global_position.is_equal_approx(Vector3(10, 2, 3)), camera.global_position)
	follower.follow_damping = 0.5
	target.position = Vector3(20, 0, 0)
	await frames(1)
	var damped := follower.global_position.x
	check("damping eases after it", damped > 10.0 and damped < 20.0, damped)
	follower.queue_free()

	var orbit := virtual_camera(world, 30)
	orbit.follow_mode = VirtualCamera3D.FOLLOW_ORBIT
	orbit.follow_target = orbit.get_path_to(target)
	orbit.follow_offset = Vector3.ZERO
	orbit.orbit_distance = 4.0
	orbit.orbit_pitch = 0.0
	orbit.orbit_yaw = 0.0
	await frames(1)
	check("an orbit is its distance from the target", camera.global_position.is_equal_approx(Vector3(20, 0, 4)), camera.global_position)
	check("looking at it", (-camera.global_basis.z).is_equal_approx(Vector3(0, 0, -1)), -camera.global_basis.z)
	orbit.orbit_yaw = PI / 2
	await frames(1)
	check("turning with its yaw", camera.global_position.is_equal_approx(Vector3(24, 0, 0)), camera.global_position)

	var wall := StaticBody3D.new()
	var shape := CollisionShape3D.new()
	shape.shape = BoxShape3D.new()
	shape.shape.size = Vector3(0.2, 4, 4)
	wall.add_child(shape)
	wall.position = Vector3(22, 0, 0)
	world.add_child(wall)
	await physics_frames(2)
	await frames(1)
	check("a wall in between pulls it in front", camera.global_position.x < 22.0 and camera.global_position.x > 20.5, camera.global_position)
	wall.queue_free()

	# Following a point on a character (its head, say): the character's own body isn't in the way.
	var character := StaticBody3D.new()
	var body := CollisionShape3D.new()
	body.shape = BoxShape3D.new()
	body.shape.size = Vector3(2, 2, 2)
	character.add_child(body)
	character.position = Vector3(30, 0, 0)
	world.add_child(character)
	var head := Node3D.new()
	head.position = Vector3(1.5, 0, 0)
	character.add_child(head)
	orbit.follow_target = orbit.get_path_to(head)
	orbit.orbit_yaw = -PI / 2
	await physics_frames(2)
	await frames(2)
	check("the character it follows doesn't pull it in", camera.global_position.is_equal_approx(Vector3(27.5, 0, 0)), camera.global_position)
	character.queue_free()
	orbit.queue_free()

	var path := Path3D.new()
	path.curve = Curve3D.new()
	path.curve.add_point(Vector3(0, 3, 10))
	path.curve.add_point(Vector3(40, 3, 10))
	world.add_child(path)
	var rail := virtual_camera(world, 40)
	rail.follow_mode = VirtualCamera3D.FOLLOW_PATH
	rail.follow_target = rail.get_path_to(target)
	rail.follow_path = rail.get_path_to(path)
	await frames(1)
	check("a rail camera sits at the closest point of its path", camera.global_position.is_equal_approx(Vector3(20, 3, 10)), camera.global_position)
	rail.queue_free()

	var second := Node3D.new()
	second.position = Vector3(30, 0, 0)
	world.add_child(second)
	var group := virtual_camera(world, 50)
	group.follow_mode = VirtualCamera3D.FOLLOW_GROUP
	group.follow_targets = [group.get_path_to(target), group.get_path_to(second)]
	group.follow_offset = Vector3(0, 0, 1)
	await frames(1)
	check("a group camera looks at the group's middle", is_equal_approx(camera.global_position.x, 25.0) and camera.global_position.z > 5.0, camera.global_position)
	check("from far enough back for both to be in view", camera.is_position_in_frustum(target.global_position) and camera.is_position_in_frustum(second.global_position))

	group.shake(1.0, 0.3)
	await frames(2)
	var shaken := camera.global_basis
	check("shaking turns the camera a little", not shaken.is_equal_approx(group.global_basis), shaken)
	await create_timer(0.4).timeout
	await frames(1)
	check("and settles", camera.global_basis.is_equal_approx(group.global_basis))
	world.queue_free()
	await frames(1)


func check_2d() -> void:
	var world := Node2D.new()
	root.add_child(world)
	var camera := Camera2D.new()
	world.add_child(camera)
	camera.make_current()
	var a := Node2D.new()
	a.position = Vector2(100, 100)
	world.add_child(a)
	var b := Node2D.new()
	b.position = Vector2(2100, 100)
	world.add_child(b)

	var follower := VirtualCamera2D.new()
	follower.follow_mode = VirtualCamera2D.FOLLOW_TARGET
	follower.blend_time = 0.0
	world.add_child(follower)
	follower.follow_target = follower.get_path_to(a)
	follower.follow_offset = Vector2(0, -20)
	await frames(2)
	check("2D: the camera follows the target", camera.global_position.is_equal_approx(Vector2(100, 80)), camera.global_position)

	var group := VirtualCamera2D.new()
	group.follow_mode = VirtualCamera2D.FOLLOW_GROUP
	group.priority = 1
	group.blend_time = 0.0
	world.add_child(group)
	group.follow_targets = [group.get_path_to(a), group.get_path_to(b)]
	await frames(1)
	check("2D: a group camera centres on the group", camera.global_position.is_equal_approx(Vector2(1100, 100)), camera.global_position)
	var view := root.get_visible_rect().size
	check("2D: and zooms out to fit it", camera.zoom.x < 1.0 and 2000.0 * camera.zoom.x <= view.x, [camera.zoom, view])
	world.queue_free()
	await frames(1)
