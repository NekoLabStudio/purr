# Started by test.gd: a run in which the player changes some options, then quits.
extends SceneTree


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	await process_frame
	Options.set_value("audio/music_volume", 0.25)
	Options.set_value("display/max_fps", 30)
	Options.set_value("game/show_hints", false)
	Options.set_value("display/graphics_tier", "medium")
	quit()
