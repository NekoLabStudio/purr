# Sends crash reports to a local copy of the online service. Start it first, with its database
# kept in a folder of its own:
#   cd online && npx wrangler d1 migrations apply purr-online --local --persist-to /tmp/purr-crashes
#   npx wrangler dev --persist-to /tmp/purr-crashes
# then run, with that folder (to read what the service stored):
#   PURR_ONLINE_STATE=/tmp/purr-crashes purr --headless --path samples/crash_reports -s online_test.gd
# It exits non-zero if a check fails.
extends SceneTree

var failures := 0


func check(what: String, ok: bool, detail: Variant = null) -> void:
	print("check: ", what, "" if ok else "  FAILED  %s" % [detail])
	if not ok:
		failures += 1


func run(script: String) -> void:
	OS.execute(OS.get_executable_path(), PackedStringArray(["--headless", "--path", ProjectSettings.globalize_path("res://"), "-s", script]), [], true)


# The newest reports the service stored, read from its database.
func stored() -> Array:
	var online := ProjectSettings.globalize_path("res://").path_join("../../online").simplify_path()
	var command := "cd '%s' && npx wrangler d1 execute purr-online --local --persist-to '%s' --json --command 'SELECT report FROM crash_reports ORDER BY created_at DESC'" % [online, OS.get_environment("PURR_ONLINE_STATE")]
	var output := []
	OS.execute("sh", PackedStringArray(["-c", command]), output)
	var parsed: Variant = JSON.parse_string("".join(output))
	return parsed[0].results if parsed is Array else []


func until(done: Callable, seconds := 20.0) -> void:
	var waited := 0.0
	while not done.call() and waited < seconds:
		await create_timer(0.25).timeout
		waited += 0.25


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	await process_frame
	ProjectSettings.set_setting("online/url", "http://localhost:8787")
	ProjectSettings.set_setting("online/game", "example")
	Options.set_value("privacy/send_crash_reports", false)
	for report in CrashReports.get_reports():
		CrashReports.delete_report(report)
	var signed_in: Dictionary = await Account.sign_in()
	check("signs in to the local service", signed_in.get("ok", false), signed_in)

	run("crash_with_details.gd")
	run("look.gd")
	var reports := CrashReports.get_reports()
	check("a crashed run left a report", reports.size() == 1, reports)
	var sent: Dictionary = await CrashReports.send(reports[0])
	check("a report can be sent", sent.get("ok", false), sent)
	check("and is removed from the device once the service has it", not FileAccess.file_exists(reports[0]))
	var text: String = stored()[0].get("report", "") if not stored().is_empty() else ""
	check("it arrives with the end of the log", text.contains("signed in as"), text)
	check("without the email or IP address, or the home folder", not text.contains("ada@example.com") and not text.contains("203.0.113.9") and not text.contains(OS.get_environment("HOME")) and text.contains("~/game.save"), text)
	var wrong: Dictionary = await CrashReports.send("user://options.cfg")
	check("something that isn't a report isn't sent", wrong.get("error") == "not_a_report", wrong)

	run("crash_with_long_log.gd")
	run("look.gd")
	sent = await CrashReports.send(CrashReports.get_reports()[0])
	check("a report longer than the service takes is sent", sent.get("ok", false), sent)
	text = stored()[0].get("report", "") if not stored().is_empty() else ""
	check("shortened, with the end of its log", text.to_utf8_buffer().size() <= 65536 and text.contains("the last line") and text.contains("didn't end cleanly"), text.length())

	run("crash.gd")
	run("look.gd")
	check("nothing is sent until the player agrees", CrashReports.get_reports().size() == 1)
	var before := stored().size()
	Options.set_value("privacy/send_crash_reports", true)
	await until(func() -> bool: return CrashReports.get_reports().is_empty())
	check("once they agree, what's waiting is sent", CrashReports.get_reports().is_empty() and stored().size() == before + 1, CrashReports.get_reports())

	Options.set_value("privacy/send_crash_reports", false)
	print("crash reports online: %d failed" % failures if failures else "crash reports online: all passed")
	quit(1 if failures else 0)
