# Saves made before the player's account was known on this device (here: kept for "local", as
# when the game first ran offline) move to the account at its first online sign-in. Against a
# local copy of the online service (see test.gd):
#   purr --headless --path samples/purr -s carry_over_test.gd
# It exits non-zero if a check fails.
extends SceneTree

const BEFORE := "user://purr/saves/local"

var failures := 0


func check(what: String, ok: bool, detail = null) -> void:
	print("check: ", what, "" if ok else "  FAILED  " + str(detail))
	if not ok:
		failures += 1


func remove_folder(path: String) -> void:
	for folder in DirAccess.get_directories_at(path):
		remove_folder(path.path_join(folder))
	for file in DirAccess.get_files_at(path):
		DirAccess.remove_absolute(path.path_join(file))
	DirAccess.remove_absolute(path)


func write(path: String, bytes: PackedByteArray) -> void:
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_buffer(bytes)


func _initialize() -> void:
	run.call_deferred()
	create_timer(30.0).timeout.connect(func():
		print("FAILED: timed out")
		quit(1))


func run() -> void:
	# A clean start: no device account, and a save made before any sign-in.
	DirAccess.remove_absolute("user://purr_credentials.cfg")
	remove_folder("user://purr")
	DirAccess.make_dir_recursive_absolute(BEFORE)
	write(BEFORE.path_join("offline.save"), var_to_bytes({"level": 3}))
	write(BEFORE.path_join("offline.json"), JSON.stringify({"dirty": true, "edits": 1, "summary": {"level": 3}}).to_utf8_buffer())

	var signed_in: Dictionary = await Account.sign_in()
	check("signs in online", signed_in.ok and signed_in.online, signed_in)
	check("the earlier save is the account's", Saves.load("offline") == {"level": 3} and Saves.get_summary("offline").level == 3, Saves.get_slots())
	check("and no longer kept apart", not FileAccess.file_exists(BEFORE.path_join("offline.save")) and not FileAccess.file_exists(BEFORE.path_join("offline.json")))

	var deleted: Dictionary = await Account.delete_account()
	check("account deleted", deleted.ok, deleted)
	print("carry over test: ", "all checks passed" if failures == 0 else "%d failed" % failures)
	quit(failures)
