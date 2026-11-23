# Started by online_test.gd: a run whose log is longer than the service takes in a report, then dies.
extends SceneTree


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	await process_frame
	for i in 200:
		print("line %d: %s" % [i, "x".repeat(1000)])
	print("the last line")
	OS.kill(OS.get_process_id())
