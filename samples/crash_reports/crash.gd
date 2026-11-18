# Started by test.gd: a run that dies without ending cleanly, as a crash or a force quit does.
extends SceneTree


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	await process_frame
	CrashReports.set_context("level", "harbour")
	print("the last thing this run says")
	OS.kill(OS.get_process_id())
