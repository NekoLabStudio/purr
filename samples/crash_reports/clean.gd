# Started by test.gd: a run that ends cleanly.
extends SceneTree


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	await process_frame
	quit()
