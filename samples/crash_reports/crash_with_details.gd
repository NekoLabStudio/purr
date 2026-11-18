# Started by online_test.gd: a run whose log has what a report mustn't take with it, then dies.
extends SceneTree


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	await process_frame
	print("signed in as ada@example.com from 203.0.113.9, saving to %s/game.save" % OS.get_environment("HOME"))
	OS.kill(OS.get_process_id())
