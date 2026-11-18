# Checks `--run-tests` and `--check-project`, by running them on this sample (where everything
# passes) and on fixture/ (where things fail on purpose). Exits non-zero if a check fails:
#   purr --headless --path samples/testing -s test.gd
extends SceneTree

var failures := 0


func check(what: String, ok: bool, detail: Variant = null) -> void:
	print("check: ", what, "" if ok else "  FAILED  %s" % [detail])
	if not ok:
		failures += 1


# Runs Purr on a project; returns its exit code and output.
func purr(project: String, arguments: PackedStringArray) -> Dictionary:
	var output := []
	var path := ProjectSettings.globalize_path("res://").path_join(project)
	var code := OS.execute(OS.get_executable_path(), PackedStringArray(["--headless", "--path", path]) + arguments, output, true)
	return {"code": code, "output": "".join(output)}


func _initialize() -> void:
	# The sample's tests use its class_name, which needs the project imported.
	purr(".", ["--import"])

	var passing := purr(".", ["--run-tests"])
	check("the sample's tests pass", passing.code == 0, passing.output)
	check("and say how many", passing.output.contains("Tests: 9 passed"), passing.output)
	check("a measure says how long a call takes", passing.output.contains("TIME  res://tests/score_test.gd adding points: ") and passing.output.contains(" a call, 95% within "), passing.output)
	var again := purr(".", ["--run-tests=score_test"])
	check("and the next run compares with the last", again.output.contains("last run's "), again.output)

	var report := ProjectSettings.globalize_path("user://report.xml")
	var failing := purr("fixture", ["--run-tests", "--test-report=" + report])
	var text: String = failing.output
	check("failing tests exit with 1", failing.code == 1, failing.code)
	check("a failed assertion names its line and says what was wrong", text.contains("FAIL  res://tests/failing_test.gd:9  test_unequal: arithmetic (expected 5, got 4)"), text)
	check("an assertion after an await still fails its test", text.contains("failing_test.gd:14  test_fails_after_waiting: expected 4 in [1, 2, 3]"), text)
	check("a script error fails its test", text.contains("test_script_error: Script error: Cannot call method 'call' on a null value."), text)
	check("a skipped test says why", text.contains("SKIP  res://tests/failing_test.gd  test_skipped: not written yet"), text)
	check("a test file that doesn't compile is reported", text.contains("FAIL  res://tests/broken_test.gd: doesn't compile"), text)
	check("so is one that isn't a TestCase", text.contains("FAIL  res://tests/not_a_case_test.gd: doesn't extend TestCase"), text)
	check("errors loading one file don't fail tests in another", not text.contains("test_passes"), text)
	check("a test that runs too long fails", text.contains("test_a_slow: Timed out after 1 s."), text)
	check("and its await finishing late doesn't end the next test early", not text.contains("test_c_check"), text)
	check("the summary counts each", text.contains("Tests: 3 passed, 4 failed, 1 skipped, 2 files couldn't run"), text)
	var xml := FileAccess.get_file_as_string(report)
	check("the report is JUnit XML", xml.begins_with("<?xml") and xml.contains("<testsuites name=\"tests\" tests=\"8\" failures=\"4\" errors=\"2\" skipped=\"1\""), xml)
	check("with a case for each test", xml.contains("<testcase name=\"test_unequal\" classname=\"tests.failing_test\""), xml)

	var filtered := purr("fixture", ["--run-tests=test_passes"])
	check("a filter can pick out one test", filtered.output.contains("Tests: 1 passed,"), filtered.output)
	var by_file := purr(".", ["--run-tests=score_test"])
	check("or one file", by_file.code == 0 and by_file.output.contains("Tests: 6 passed"), by_file.output)
	var nothing := purr(".", ["--run-tests=no_such_thing"])
	check("a filter matching nothing fails, so a typo isn't a pass", nothing.code == 1 and nothing.output.contains("No tests match \"no_such_thing\"."), nothing.output)

	var clean := purr(".", ["--check-project"])
	check("the sample passes the check", clean.code == 0 and clean.output.contains("0 errors, 0 warnings"), clean.output)
	var broken := purr("fixture", ["--check-project"])
	text = broken.output
	check("a project with errors fails the check", broken.code == 1, broken.code)
	check("a missing main scene is an error", text.contains("ERROR    project.godot: the main scene is res://missing.tscn, which isn't there."), text)
	check("so is a missing autoload", text.contains("the autoload Ghost is res://ghost.gd, which isn't there."), text)
	check("and a scene needing a missing file", text.contains("ERROR    res://broken.tscn: needs res://gone.png, which isn't there."), text)
	check("and a script that doesn't compile", text.contains("ERROR    res://tests/broken_test.gd: doesn't compile"), text)
	check("nodes' configuration warnings are warnings", text.contains("WARNING  res://warned.tscn: Warned/Shape: A shape must be provided"), text)
	check("an excluded folder isn't checked", not text.contains("res://addons/"), text)
	check("but one whose name only starts the same is", text.contains("ERROR    res://addons_extra/broken.gd: doesn't compile"), text)
	check("the summary counts them", text.contains("5 errors, 1 warning."), text)

	print("testing: %d failed" % failures if failures else "testing: all passed")
	quit(1 if failures else 0)
