# Checks Options, in this run and across runs of the game in other processes. Exits non-zero if a
# check fails:
#   purr --headless --path samples/options -s test.gd
extends SceneTree

var failures := 0


func check(what: String, ok: bool, detail: Variant = null) -> void:
	print("check: ", what, "" if ok else "  FAILED  %s" % [detail])
	if not ok:
		failures += 1


func run(script: String) -> Dictionary:
	var output := []
	OS.execute(OS.get_executable_path(), PackedStringArray(["--headless", "--path", ProjectSettings.globalize_path("res://"), "-s", script]), output, true)
	for line in "\n".join(output).split("\n"):
		if line.begins_with("RESULT "):
			return JSON.parse_string(line.trim_prefix("RESULT "))
	return {}


func remove_files() -> void:
	for file in ["options.cfg", "options.bad.cfg"]:
		DirAccess.remove_absolute("user://" + file)


func _until(done: Callable, seconds := 5.0) -> void:
	var waited := 0.0
	while not done.call() and waited < seconds:
		await create_timer(0.1).timeout
		waited += 0.1


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	await process_frame
	remove_files()

	# Across runs.
	var fresh := run("look.gd")
	check("a first run has the defaults, from the project and the bus layout", is_equal_approx(fresh.get("music", 0.0), 0.5) and fresh.get("show_hints") == true and fresh.get("engine_max_fps") == 0.0, fresh)
	run("choose.gd")
	var chosen := run("look.gd")
	check("the player's choices are there in the next run", chosen.get("music") == 0.25 and chosen.get("show_hints") == false and chosen.get("max_fps") == 30.0, chosen)
	check("the graphics tier too, before the window opens", chosen.get("tier") == "medium" and chosen.get("lod_threshold") == 2.0, chosen)
	check("and in effect as it starts", is_equal_approx(chosen.get("music_bus", 0.0), 0.25) and chosen.get("engine_max_fps") == 30.0, chosen)
	check("only what differs from the default is saved", chosen.get("file", "").contains("music_volume=0.25") and not chosen.get("file", "").contains("text_size"), chosen.get("file"))

	var file := FileAccess.open("user://options.cfg", FileAccess.WRITE)
	file.store_string("[audio\nnot a config file = = =")
	file.close()
	var bad := run("look.gd")
	check("a file that can't be read gives the defaults and is set aside", bad.get("show_hints") == true and bad.get("set_aside") == true, bad)
	remove_files()

	# In this run.
	var keys := Options.get_options()
	check("the engine's options are there", keys.has("display/window_mode") and keys.has("display/vsync") and keys.has("display/max_fps") and keys.has("display/graphics_tier") and keys.has("audio/master_volume") and keys.has("audio/music_volume"), keys)
	check("and the project's", Options.get_value("game/text_size") == 16 and Options.get_default("game/show_hints") == true, keys)
	var tiers := ["ultra", "high", "medium", "low"]
	var tier: String = Options.get_value("display/graphics_tier")
	check("the graphics tier starts as the one recommended", tier == tiers[RecommendedGraphics.get_tier()], tier)
	check("and its settings are the window's", root.mesh_lod_threshold == RecommendedGraphics.get_settings(RecommendedGraphics.get_tier()).mesh_lod_threshold, root.mesh_lod_threshold)
	Options.set_value("display/graphics_tier", "ultra")
	check("changing it changes the window", root.mesh_lod_threshold == 1.0 and ProjectSettings.get_setting("rendering/mesh_lod/lod_change/threshold_pixels") == 1.0 and RecommendedGraphics.get_tier() != RecommendedGraphics.TIER_ULTRA, root.mesh_lod_threshold)
	check("ultra has contact shadows", ProjectSettings.get_setting("rendering/lights_and_shadows/contact_shadow/enabled") == true)
	Options.set_value("display/graphics_tier", "low")
	check("low doesn't", ProjectSettings.get_setting("rendering/lights_and_shadows/contact_shadow/enabled") == false)
	check("and loads textures without their sharpest level", ProjectSettings.get_setting("rendering/textures/quality/skip_mipmaps") == 1)
	check("and a headset never does", RecommendedGraphics.get_settings(RecommendedGraphics.TIER_ULTRA, true).contact_shadows == false)
	Options.set_value("display/graphics_tier", "ultra")
	check("window modes are choices", Options.get_info("display/window_mode").get("choices") == PackedStringArray(["windowed", "fullscreen", "exclusive_fullscreen"]), Options.get_info("display/window_mode"))

	var changes := []
	Options.changed.connect(func(key: String, value: Variant) -> void: changes.append([key, value]))
	Options.define("game/difficulty", "normal", { "choices": ["easy", "normal", "hard"] })
	Options.set_value("game/difficulty", "hard")
	Options.set_value("game/difficulty", "hard")
	check("a change is announced once", changes == [["game/difficulty", "hard"]], changes)
	Options.set_value("game/difficulty", "impossible")
	check("a value that isn't a choice is refused", Options.get_value("game/difficulty") == "hard")
	Options.set_value("audio/music_volume", 3)
	check("numbers are kept in range", Options.get_value("audio/music_volume") == 1.0 and is_equal_approx(AudioServer.get_bus_volume_linear(1), 1.0), Options.get_value("audio/music_volume"))
	Options.set_value("display/max_fps", 45)
	check("the frame rate cap changes as it's set", Engine.max_fps == 45, Engine.max_fps)
	Options.reset_all()
	check("resetting gives back the defaults", Options.get_value("game/difficulty") == "normal" and is_equal_approx(AudioServer.get_bus_volume_linear(1), 0.5) and Engine.max_fps == 0)

	Options.set_value("game/text_size", 20)
	await process_frame
	root.propagate_notification(NOTIFICATION_APPLICATION_PAUSED)
	check("going to the background writes what's changed at once", FileAccess.get_file_as_string("user://options.cfg").contains("text_size=20"), FileAccess.get_file_as_string("user://options.cfg"))
	Options.reset("game/text_size")

	if DisplayServer.get_name() != "headless":
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_FULLSCREEN)
		await _until(func() -> bool: return Options.get_value("display/window_mode") == "fullscreen")
		check("going fullscreen another way shows in the option", Options.get_value("display/window_mode") == "fullscreen", Options.get_value("display/window_mode"))
		DisplayServer.window_set_mode(DisplayServer.WINDOW_MODE_WINDOWED)
		await _until(func() -> bool: return Options.get_value("display/window_mode") == "windowed")
		check("and so does coming back", Options.get_value("display/window_mode") == "windowed", Options.get_value("display/window_mode"))

	# Written within a second of the last change.
	for i in 90:
		await process_frame
	await create_timer(1.2).timeout
	var written := FileAccess.get_file_as_string("user://options.cfg")
	check("the file is written soon after", FileAccess.file_exists("user://options.cfg") and not written.contains("difficulty"), written)
	remove_files()

	print("options: %d failed" % failures if failures else "options: all passed")
	quit(1 if failures else 0)
