# Started by test.gd: the next run, which prints its options and what the engine took from them.
extends SceneTree


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	await process_frame
	print("RESULT " + JSON.stringify({
		"music": Options.get_value("audio/music_volume"),
		"music_bus": AudioServer.get_bus_volume_linear(AudioServer.get_bus_index("Music")),
		"max_fps": Options.get_value("display/max_fps"),
		"engine_max_fps": Engine.max_fps,
		"show_hints": Options.get_value("game/show_hints"),
		"tier": Options.get_value("display/graphics_tier"),
		"lod_threshold": root.mesh_lod_threshold,
		"file": FileAccess.get_file_as_string("user://options.cfg"),
		"set_aside": FileAccess.file_exists("user://options.bad.cfg"),
	}))
	quit()
