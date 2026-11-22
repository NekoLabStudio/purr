# Checks Store as it works in the editor without a store: store/owned_in_editor says what's owned,
# purchases succeed and say so, and IDs the Store tab doesn't list are refused. Exits non-zero if a
# check fails:
#   purr --headless --path samples/store -s test.gd
extends SceneTree

var failures := 0
var changed: Array[String] = []


func check(what: String, ok: bool, detail: Variant = null) -> void:
	print("check: ", what, "" if ok else "  FAILED  %s" % [detail])
	if not ok:
		failures += 1


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	Store.ownership_changed.connect(func(id): changed.append(id))
	check("the editor has a pretend store", Store.is_available())
	check("what store/owned_in_editor lists is owned", Store.owns("soundtrack"))
	check("and nothing else", not Store.owns("winter_expansion"))
	check("the editor knows no prices", Store.get_price("winter_expansion") == "")

	var bought: Dictionary = await Store.purchase("winter_expansion")
	check("a purchase in the editor succeeds", bought.ok, bought)
	check("and the item is owned", Store.owns("winter_expansion"))
	await process_frame
	check("ownership_changed says so", changed == ["winter_expansion"], changed)

	var again: Dictionary = await Store.purchase("soundtrack")
	check("buying what's owned succeeds at once", again.ok, again)

	var unknown: Dictionary = await Store.purchase("no_such_item")
	check("an ID not in the Store tab is refused", not unknown.ok and unknown.error == "unavailable", unknown)

	var restored: Dictionary = await Store.restore()
	check("restore succeeds", restored.ok, restored)
	check("and keeps what's owned", Store.owns("soundtrack") and Store.owns("winter_expansion"))

	print("store test: %s" % ("all checks passed" if failures == 0 else "%d failed" % failures))
	quit(failures)
