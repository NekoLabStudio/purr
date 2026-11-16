# Checks Viewport.local_focus (patch 0028). Exits non-zero if a check fails:
#   purr --headless --path samples/split_focus -s test.gd
extends SceneTree

var failures := 0


func check(what: String, ok: bool) -> void:
	print("check: ", what, "" if ok else "  FAILED")
	if not ok:
		failures += 1


func player(local_focus: bool) -> Array:
	var viewport := SubViewport.new()
	viewport.local_focus = local_focus
	root.add_child(viewport)
	var buttons := []
	for i in 2:
		var button := Button.new()
		viewport.add_child(button)
		buttons.append(button)
	return buttons


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var a := player(true)
	var b := player(true)
	var plain := player(false)
	var root_button := Button.new()
	root.add_child(root_button)

	a[0].grab_focus()
	b[0].grab_focus()
	check("a player's focus survives another player taking focus", a[0].has_focus() and b[0].has_focus())

	var exited := [false]
	a[0].focus_exited.connect(func(): exited[0] = true)
	a[1].grab_focus()
	check("focus moves within one player's viewport, with focus_exited", a[1].has_focus() and not a[0].has_focus() and exited[0])

	root_button.grab_focus()
	check("the window's own focus leaves the players' alone", root_button.has_focus() and a[1].has_focus() and b[0].has_focus())

	plain[0].grab_focus()
	check("a viewport without local_focus takes the window's focus", plain[0].has_focus() and not root_button.has_focus())
	check("and still leaves the players' alone", a[1].has_focus() and b[0].has_focus())

	root_button.grab_focus()
	check("a viewport without local_focus loses focus as before", root_button.has_focus() and not plain[0].has_focus())

	# Out of the tree and back: the players' focus survives it.
	var viewport: Viewport = a[1].get_viewport()
	root.remove_child(viewport)
	root.add_child(viewport)
	a[0].grab_focus()
	b[1].grab_focus()
	check("local_focus holds after the viewport re-enters the tree", a[0].has_focus() and b[1].has_focus())

	quit(1 if failures > 0 else 0)
