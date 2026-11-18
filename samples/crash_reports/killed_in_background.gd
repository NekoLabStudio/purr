# Started by test.gd: a run the system sends to the background, then closes, as phones do.
extends SceneTree


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	for i in 3:
		await process_frame
	root.propagate_notification(NOTIFICATION_APPLICATION_PAUSED)
	OS.kill(OS.get_process_id())
