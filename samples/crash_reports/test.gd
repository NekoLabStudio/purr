# Checks CrashReports by running the game in other processes. Exits non-zero if a check fails:
#   purr --headless --path samples/crash_reports -s test.gd
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


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	await process_frame
	for report in CrashReports.get_reports():
		CrashReports.delete_report(report)

	run("clean.gd")
	var after_clean := run("look.gd")
	check("a run that ends cleanly leaves no report", after_clean.get("crashed") == false and after_clean.get("reports") == 0, after_clean)

	run("crash.gd")
	var after_crash := run("look.gd")
	var report: String = after_crash.get("latest", "")
	check("the next run knows the one before crashed", after_crash.get("crashed") == true and after_crash.get("reports") == 1, after_crash)
	check("the report says what it ran on", report.contains("Engine: Purr v") and report.contains("Game: Crash reports sample 1.2.3") and report.contains("OS: "), report)
	check("with the game's own context", report.contains("level: harbour"), report)
	check("and the end of its log", report.contains("the last thing this run says"), report)

	# This test is a run too, still going: the runs it starts mustn't take it for a crash.
	var again := run("look.gd")
	check("a crash is reported once, and a run still going isn't one", again.get("crashed") == false and again.get("reports") == 1, again)

	run("killed_in_background.gd")
	var after_background := run("look.gd")
	check("a run closed in the background, as phones do, isn't a crash", after_background.get("crashed") == false and after_background.get("reports") == 1, after_background)

	print("crash reports: %d failed" % failures if failures else "crash reports: all passed")
	quit(1 if failures else 0)
