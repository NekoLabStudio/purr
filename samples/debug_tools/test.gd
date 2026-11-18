# Checks DebugDraw and Console. Exits non-zero if a check fails:
#   purr --headless --path samples/debug_tools -s test.gd
extends SceneTree

var failures := 0
var teleported := Vector3.ZERO
var greeting := ""


func check(what: String, ok: bool, detail: Variant = null) -> void:
	print("check: ", what, "" if ok else "  FAILED  %s" % [detail])
	if not ok:
		failures += 1


func frames(count: int) -> void:
	for i in count:
		await process_frame


var cell := Vector2i.ZERO


func goto_cell(where: Vector2i) -> void:
	cell = where


func teleport(where: Vector3, height := 0.0) -> void:
	teleported = where + Vector3(0, height, 0)


func greet(who: String, times: int) -> String:
	var words := PackedStringArray()
	for i in times:
		words.append("hello " + who)
	greeting = ", ".join(words)
	return greeting


func busy_work() -> void:
	var total := 0
	for i in 2000:
		total += i


func set_flag(on: bool) -> void:
	greeting = "on" if on else "off"


# Drawing from worker threads while frames go by.
func check_threads() -> void:
	DebugDraw.clear()
	var task := WorkerThreadPool.add_group_task(func(i: int) -> void:
		for j in 200:
			DebugDraw.line(Vector3(i, j, 0), Vector3(i, j, 1), Color.RED, 5.0)
		, 8)
	while not WorkerThreadPool.is_group_task_completed(task):
		await process_frame
	WorkerThreadPool.wait_for_group_task_completion(task)
	check("drawing from several threads at once keeps every line", DebugDraw.get_line_count() == 1600, DebugDraw.get_line_count())
	DebugDraw.clear()


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	await frames(2)
	await check_debug_draw()
	await check_console()
	await check_threads()
	print("debug tools: %d failed" % failures if failures else "debug tools: all passed")
	quit(1 if failures else 0)


func check_debug_draw() -> void:
	check("on in debug builds", DebugDraw.enabled)

	DebugDraw.line(Vector3.ZERO, Vector3.ONE)
	DebugDraw.box(Transform3D.IDENTITY)
	DebugDraw.line_2d(Vector2.ZERO, Vector2(10, 10))
	check("a line, a box's 12 edges and a 2D line wait to be drawn", DebugDraw.get_line_count() == 14, DebugDraw.get_line_count())
	await frames(1)
	check("one frame's shapes go with the frame", DebugDraw.get_line_count() == 0, DebugDraw.get_line_count())

	DebugDraw.sphere(Vector3.ZERO, 1.0, Color.RED, 0.3)
	await frames(3)
	check("shapes drawn for a while stay", DebugDraw.get_line_count() == 96, DebugDraw.get_line_count())
	await create_timer(0.4).timeout
	await frames(1)
	check("and go when their time is up", DebugDraw.get_line_count() == 0, DebugDraw.get_line_count())

	# Drawn during a physics tick: kept until the next tick, however many frames pass.
	await physics_frame
	DebugDraw.arrow(Vector3.ZERO, Vector3.UP)
	check("an arrow is a line and a four-stroke head", DebugDraw.get_line_count() == 5, DebugDraw.get_line_count())
	await process_frame
	await physics_frame
	check("a tick's shapes go at the next tick", DebugDraw.get_line_count() == 0, DebugDraw.get_line_count())

	DebugDraw.path(PackedVector3Array([Vector3.ZERO, Vector3.RIGHT, Vector3.UP]), Color.WHITE, 1.0, true)
	check("a closed path of three points has three lines", DebugDraw.get_line_count() == 3, DebugDraw.get_line_count())
	DebugDraw.clear()
	check("clear() takes everything away", DebugDraw.get_line_count() == 0, DebugDraw.get_line_count())

	DebugDraw.enabled = false
	DebugDraw.line(Vector3.ZERO, Vector3.ONE, Color.WHITE, 5.0)
	DebugDraw.show_value("ignored", 1)
	check("turned off, nothing is kept", DebugDraw.get_line_count() == 0, DebugDraw.get_line_count())
	DebugDraw.enabled = true

	DebugDraw.show_value("speed", 12.3456)
	DebugDraw.text(Vector3.ZERO, "label")
	DebugDraw.text_2d(Vector2.ZERO, "label")
	await frames(1)


func check_console() -> void:
	check("on in debug builds", Console.enabled)
	Console.add_command("teleport", teleport, "Moves the player.")
	Console.add_command("goto", goto_cell)
	Console.add_command("greet", greet)
	Console.add_command("flag", set_flag)
	Console.add_command("double", func(value): return value * 2)
	check("commands are listed with the built-in ones, sorted", Console.get_commands() == PackedStringArray(["clear", "double", "flag", "goto", "greet", "help", "perf", "profile", "teleport"]), Console.get_commands())

	Console.run("teleport 1,2,3")
	check("arguments become the types the method declares", teleported == Vector3(1, 2, 3), teleported)
	Console.run("teleport 1,2,3 4")
	check("optional arguments can be given", teleported == Vector3(1, 6, 3), teleported)
	Console.run("perf")
	check("perf shows the performance overlay", DebugDraw.performance_overlay)
	Console.run("perf")
	check("and hides it again", not DebugDraw.performance_overlay)
	Console.run("goto 4,-7")
	check("whole-number vectors are written the same way", cell == Vector2i(4, -7), cell)
	Console.run("goto 4.5,7")
	check("but only take whole numbers", cell == Vector2i(4, -7), cell)
	var result: Variant = Console.run("greet \"Ada Lovelace\" 2")
	check("quotes keep spaces in one argument, and the result comes back", result == "hello Ada Lovelace, hello Ada Lovelace", result)
	check("and is written to the console", Console.get_text().contains("hello Ada Lovelace, hello Ada Lovelace"))
	Console.run("flag off")
	check("a bool can be on or off", greeting == "off", greeting)
	check("a lambda's arguments are guessed from the text", Console.run("double 21") == 42)

	Console.clear()
	Console.run("teleport")
	check("too few arguments shows how to use it", Console.get_text().contains("Usage: teleport <where: Vector3> [height: float]"), Console.get_text())
	Console.clear()
	Console.run("greet Ada lots")
	check("a wrong type says so", Console.get_text().contains("\"lots\" isn't a valid int"), Console.get_text())
	Console.clear()
	Console.run("fly")
	check("an unknown command says so", Console.get_text().contains("No command called \"fly\""), Console.get_text())
	Console.clear()
	Console.run("help")
	check("help lists commands with what they do", Console.get_text().contains("teleport <where: Vector3> [height: float]  Moves the player."), Console.get_text())

	Console.clear()
	Console.run("profile 0")
	check("profile takes a number of seconds", Console.get_text().contains("Usage: profile [seconds]"), Console.get_text())
	Console.clear()
	process_frame.connect(busy_work)
	Console.run("profile 1")
	Console.run("profile")
	check("one profile at a time", Console.get_text().contains("Already profiling: 1 s to go."), Console.get_text())
	var waited := 0.0
	while not Console.get_text().contains("The whole report: ") and waited < 5.0:
		await create_timer(0.1).timeout
		waited += 0.1
	process_frame.disconnect(busy_work)
	var text := Console.get_text()
	check("a profile says how the frames went", text.contains("Frames: ") and text.contains("at the middle, 95% within "), text)
	check("and which script functions cost most", text.contains("res://test.gd:") and text.contains(" busy_work ("), text)
	var reports := DirAccess.get_files_at("user://profiles")
	var report := FileAccess.get_file_as_string("user://profiles".path_join(reports[reports.size() - 1])) if reports.size() > 0 else ""
	check("and saves the whole report", report.begins_with("Profile of ") and report.contains("busy_work"), report)
	for file in reports:
		DirAccess.remove_absolute("user://profiles".path_join(file))

	Console.clear()
	print("printed by the game")
	push_warning("a warning")
	await frames(1)
	check("what the game prints shows in the console", Console.get_text().contains("printed by the game"), Console.get_text())
	check("and warnings", Console.get_text().contains("a warning"), Console.get_text())

	Console.remove_command("teleport")
	check("a command can be removed", not Console.has_command("teleport"))
	var owner := Node.new()
	Console.add_command("owned", owner.get_name)
	owner.free()
	check("a command whose object was freed is forgotten", not Console.has_command("owned") and not Console.get_commands().has("owned"))

	Console.visible = true
	await frames(1)
	var view := root.get_node_or_null("Console")
	check("the console opens on screen", view != null and view.visible)
	var typed := InputEventKey.new()
	typed.keycode = KEY_W
	typed.physical_keycode = KEY_W
	typed.pressed = true
	Input.parse_input_event(typed)
	Input.flush_buffered_events()
	check("keys typed into it aren't the game's", not Input.is_physical_key_pressed(KEY_W))
	typed.pressed = false
	Input.parse_input_event(typed)
	Input.flush_buffered_events()
	Console.visible = false
	check("and closes", view != null and not view.visible)
	typed.pressed = true
	Input.parse_input_event(typed)
	Input.flush_buffered_events()
	check("giving the keys back", Input.is_physical_key_pressed(KEY_W))
	typed.pressed = false
	Input.parse_input_event(typed)
	Input.flush_buffered_events()
