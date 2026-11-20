# Checks FrameBudget. Exits non-zero if a check fails:
#   purr --headless --path samples/graphics -s test.gd
extends SceneTree

var failures := 0
var steps := 0
var long_frames: Array[Dictionary] = []


func check(what: String, ok: bool, detail: Variant = null) -> void:
	print("check: ", what, "" if ok else "  FAILED  %s" % [detail])
	if not ok:
		failures += 1


func busy(milliseconds: float) -> void:
	var until := Time.get_ticks_usec() + int(milliseconds * 1000.0)
	while Time.get_ticks_usec() < until:
		pass


func frames(count: int) -> void:
	for i in count:
		await process_frame


func step_slowly() -> bool:
	steps += 1
	busy(0.25)
	return steps < 1000


func step_once() -> void:
	steps += 1


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	if DisplayServer.get_name() == "headless":
		check("quiet headless, where frames draw nothing", not FrameBudget.reporting)
	FrameBudget.reporting = true
	FrameBudget.long_frame.connect(func(milliseconds: float, work: Dictionary) -> void:
		long_frames.append({"ms": milliseconds, "work": work}))

	# A long frame says what took its time.
	await frames(2)
	FrameBudget.long_frame_ms = 15.0
	var started := Time.get_ticks_usec()
	busy(25.0)
	FrameBudget.measure("pathfinding", started)
	await frames(2)
	var long: Dictionary = long_frames.back() if not long_frames.is_empty() else {}
	check("a long frame is reported with its measured work", long.get("ms", 0.0) >= 25.0 and long.get("work", {}).get("pathfinding", 0.0) >= 24.0, long)
	# Far above the odd stall a busy machine (a CI runner) has with nothing running.
	FrameBudget.long_frame_ms = 250.0
	long_frames.clear()
	await frames(3)
	check("short frames aren't", long_frames.is_empty(), long_frames)
	FrameBudget.long_frame_ms = 1000.0

	# Spread work runs within its budget each frame, until its step says it's done.
	steps = 0
	FrameBudget.spread("building", step_slowly, 2.0)
	check("spread work waits for the frame", steps == 0 and FrameBudget.get_spread_count() == 1)
	await frames(1)
	var in_a_frame := steps
	check("a frame's share stays near its budget", in_a_frame >= 2 and in_a_frame <= 12, in_a_frame)
	FrameBudget.screen_hidden = true
	var before := steps
	await frames(1)
	var hidden := steps - before
	FrameBudget.screen_hidden = false
	check("more is done while the screen's hidden", hidden > in_a_frame * 2, [in_a_frame, hidden])
	while FrameBudget.get_spread_count() > 0:
		await frames(1)
	check("the job ends when its step says so", steps == 1000)

	steps = 0
	FrameBudget.spread("once", step_once)
	await frames(2)
	check("a step that returns nothing runs once", steps == 1 and FrameBudget.get_spread_count() == 0)

	var holder := Node.new()
	FrameBudget.spread("gone", holder.queue_free)
	holder.free()
	await frames(1)
	check("a job whose object's gone is dropped", FrameBudget.get_spread_count() == 0)

	await _screen_effects()
	await _color_grading()
	if DisplayServer.get_name() != "headless":
		await _stereo_occlusion()
		await _hidden_occluders_freed()

	print("graphics test: %s" % ("all checks passed" if failures == 0 else "%d failed" % failures))
	quit(failures)



# The screen effects: added to a compositor like any compositor effect; in a window, what they draw.
func _screen_effects() -> void:
	var effects: Array[CompositorEffect] = [MotionBlurEffect.new(), LightShaftsEffect.new(), LensFlareEffect.new(), VignetteEffect.new(), FilmGrainEffect.new(), ChromaticAberrationEffect.new(), LocalExposureEffect.new(), WetSurfacesEffect.new()]
	var saved := effects.all(func(effect: CompositorEffect) -> bool:
		for property in effect.get_property_list():
			if property.name == "effect_callback_type" and property.usage & PROPERTY_USAGE_STORAGE:
				return false
		return true)
	check("screen effects keep their drawing to themselves, not saved", saved)
	var vignette := VignetteEffect.new()
	vignette.strength = 3.0
	check("a vignette's strength stays within 0 and 1", vignette.strength == 1.0, vignette.strength)
	var wet := WetSurfacesEffect.new()
	wet.puddles = 2.0
	check("wet surfaces' puddles stay within 0 and 1", wet.puddles == 1.0)
	check("and they're drawn before transparent things, after the sky", wet.effect_callback_type == CompositorEffect.EFFECT_CALLBACK_TYPE_POST_SKY and wet.needs_normal_roughness)

	var view := SubViewport.new()
	view.size = Vector2i(64, 64)
	view.own_world_3d = true
	view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(view)
	var camera := Camera3D.new()
	view.add_child(camera)
	var wall := MeshInstance3D.new()
	wall.mesh = QuadMesh.new()
	wall.mesh.size = Vector2(20, 20)
	var white := StandardMaterial3D.new()
	white.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	wall.material_override = white
	wall.position = Vector3(0, 0, -2)
	view.add_child(wall)
	var environment := WorldEnvironment.new()
	environment.environment = Environment.new()
	environment.compositor = Compositor.new()
	environment.compositor.compositor_effects = effects
	view.add_child(environment)
	await frames(4)
	check("they draw with the scene without errors", true)
	if DisplayServer.get_name() != "headless":
		environment.compositor.compositor_effects = [vignette]
		await frames(4)
		var image := view.get_texture().get_image()
		check("a vignette darkens the corners", image.get_pixel(1, 1).get_luminance() < image.get_pixel(32, 32).get_luminance() - 0.2, [image.get_pixel(1, 1), image.get_pixel(32, 32)])
	view.queue_free()



# In a headset, each eye is culled from where it is: what one eye sees past a wall's edge is drawn.
func _stereo_occlusion() -> void:
	var stereo: XRInterfaceExtension = load("res://stereo_occlusion.gd").new()
	XRServer.add_interface(stereo)
	stereo.initialize()
	XRServer.primary_interface = stereo
	var view := SubViewport.new()
	view.size = Vector2i(256, 256)
	view.own_world_3d = true
	view.use_xr = true
	view.use_occlusion_culling = true
	view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(view)
	var origin := XROrigin3D.new()
	view.add_child(origin)
	origin.add_child(XRCamera3D.new())
	# A wall a metre ahead whose edge is straight in front, as an occluder.
	var wall := OccluderInstance3D.new()
	wall.occluder = QuadOccluder3D.new()
	wall.occluder.size = Vector2(100, 100)
	wall.position = Vector3(-50, 0, -1)
	view.add_child(wall)
	var drawn_at := func(x: float) -> int:
		var box := MeshInstance3D.new()
		box.mesh = BoxMesh.new()
		box.position = Vector3(x, 0, -20)
		view.add_child(box)
		for i in 10:
			await process_frame
		var objects := RenderingServer.viewport_get_render_info(view.get_viewport_rid(), RenderingServer.VIEWPORT_RENDER_INFO_TYPE_VISIBLE, RenderingServer.VIEWPORT_RENDER_INFO_OBJECTS_IN_FRAME)
		box.free()
		return objects
	check("a headset draws what only one eye sees past a wall's edge", await drawn_at.call(-1.5) > 0)
	check("and still culls what neither eye sees", await drawn_at.call(-8.0) == 0)
	view.queue_free()
	XRServer.primary_interface = null
	stereo.uninitialize()
	XRServer.remove_interface(stereo)



# An occluder hidden and then freed leaves nothing behind (its triangles were kept for good).
func _hidden_occluders_freed() -> void:
	var view := SubViewport.new()
	view.size = Vector2i(64, 64)
	view.own_world_3d = true
	view.use_occlusion_culling = true
	view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(view)
	view.add_child(Camera3D.new())
	var points := PackedVector3Array()
	var indices := PackedInt32Array()
	for i in 20000:
		points.append(Vector3(randf(), randf(), randf()))
	for i in 60000:
		indices.append(randi() % 20000)
	var made := func() -> void:
		for i in 40:
			var occluder := OccluderInstance3D.new()
			occluder.occluder = ArrayOccluder3D.new()
			occluder.occluder.set_arrays(points, indices)
			view.add_child(occluder)
			await process_frame
			occluder.hide()
			await process_frame
			occluder.free()
		for i in 5:
			await process_frame
	await made.call()  # once, so what's made once and kept is counted before
	var before := OS.get_static_memory_usage()
	await made.call()
	var grown := OS.get_static_memory_usage() - before
	check("occluders hidden before they're freed leave nothing behind", grown < 4 * 1024 * 1024, "%d MB" % (grown / 1048576))
	view.queue_free()


# Grades blended into the camera's environment by place, and by time of day.
func _color_grading() -> void:
	var holder := Node3D.new()
	root.add_child(holder)
	var environment := Environment.new()
	environment.adjustment_saturation = 1.0
	var world_environment := WorldEnvironment.new()
	world_environment.environment = environment
	holder.add_child(world_environment)
	var camera := Camera3D.new()
	holder.add_child(camera)
	camera.make_current()
	var cave := ColorGradeVolume3D.new()
	cave.grade = ColorGrade.new()
	cave.grade.saturation = 0.2
	cave.grade.exposure = 0.5
	cave.size = Vector3(10, 10, 10)
	cave.blend_distance = 4.0
	cave.position = Vector3(100, 0, 0)
	holder.add_child(cave)
	await frames(2)
	check("outside a volume, the environment is its own", is_equal_approx(environment.adjustment_saturation, 1.0) and is_equal_approx(environment.tonemap_exposure, 1.0))
	camera.global_position = Vector3(100, 0, 0)
	await frames(2)
	check("inside, it's the volume's grade", environment.adjustment_enabled and is_equal_approx(environment.adjustment_saturation, 0.2) and is_equal_approx(environment.tonemap_exposure, 0.5), [environment.adjustment_saturation, environment.tonemap_exposure])
	camera.global_position = Vector3(107, 0, 0)
	await frames(2)
	check("near its edge, part way", environment.adjustment_saturation > 0.2 and environment.adjustment_saturation < 1.0, environment.adjustment_saturation)
	var warm := ColorGradeVolume3D.new()
	warm.grade = ColorGrade.new()
	warm.grade.tint = Color(1.0, 0.8, 0.6)
	warm.global = true
	warm.when = ColorGradeVolume3D.WHEN_NIGHT
	holder.add_child(warm)
	var day := TimeOfDay.new()
	day.day_length = 0.0
	day.time = 12.0
	holder.add_child(day)
	camera.global_position = Vector3.ZERO
	await frames(2)
	check("a night grade isn't there by day", environment.adjustment_color_correction == null)
	day.time = 0.0
	await frames(2)
	var table := environment.adjustment_color_correction as Texture3D
	check("at night its tint is a colour lookup table", table != null)
	# The headless renderer keeps no 3D textures' data to read back.
	if table != null and table.get_data().size() == table.get_depth():
		var white := table.get_data()[table.get_depth() - 1].get_pixel(table.get_width() - 1, table.get_height() - 1)
		check("that tints white", white.is_equal_approx(Color(1.0, 0.8, 0.6)), white)
	holder.remove_child(cave)
	holder.remove_child(warm)
	await frames(1)
	check("with no volumes left, the environment is its own again", is_equal_approx(environment.adjustment_saturation, 1.0) and environment.adjustment_color_correction == null and is_equal_approx(environment.tonemap_exposure, 1.0))
	cave.free()
	warm.free()
	holder.queue_free()
