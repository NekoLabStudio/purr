# Started by test.gd: the next run, which prints what CrashReports found.
extends SceneTree


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	await process_frame
	var reports := CrashReports.get_reports()
	var latest := FileAccess.get_file_as_string(reports[0]) if not reports.is_empty() else ""
	print("RESULT " + JSON.stringify({ "crashed": CrashReports.previous_run_crashed(), "reports": reports.size(), "latest": latest }))
	quit()
