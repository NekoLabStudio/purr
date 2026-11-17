# Tries InputBindings headless: the defaults from the Input Map and controls.json, reading
# actions per player, contexts, saving and resetting, capturing, describing and prompts in both
# styles. Joypads can't be plugged in headless; try those with the controls screen
# (main.tscn) and a controller.
#   purr --headless --path samples/controls -s test.gd
# It exits non-zero if a check fails.
extends SceneTree

var failures := 0


func check(what: String, ok: bool, detail = null) -> void:
	print("check: ", what, "" if ok else "  FAILED  " + str(detail))
	if not ok:
		failures += 1


func key(code: Key, pressed: bool) -> void:
	var event := InputEventKey.new()
	event.physical_keycode = code
	event.pressed = pressed
	Input.parse_input_event(event)


func mouse(button: MouseButton, pressed: bool) -> void:
	var event := InputEventMouseButton.new()
	event.button_index = button
	event.pressed = pressed
	Input.parse_input_event(event)


func frames(count := 2) -> void:
	for i in count:
		await process_frame


func _initialize() -> void:
	# Before the first frame, as a game's own autoloads start: the defaults are already there.
	check("the defaults are there before the first frame", InputBindings.get_bindings("jump").size() == 2, InputBindings.get_bindings("jump"))
	run.call_deferred()
	create_timer(20.0).timeout.connect(func():
		print("FAILED: timed out")
		quit(1))


func run() -> void:
	# A clean start, with a game's own image for the space bar.
	DirAccess.remove_absolute("user://input_bindings/default.json")
	DirAccess.remove_absolute("user://input_bindings/guest.json")
	var own := InputPromptSet.new()
	own.images = {"keyboard/space": ImageTexture.create_from_image(Image.create_empty(7, 7, false, Image.FORMAT_RGBA8))}
	ResourceSaver.save(own, "user://own_prompts.tres")
	ProjectSettings.set_setting("input_devices/prompts/custom_set", "user://own_prompts.tres")
	await frames()

	var actions := InputBindings.get_actions()
	check("the game's actions, from the Input Map and the defaults file", actions == PackedStringArray(["accelerate", "fire", "honk", "jump", "move_left", "move_right"]), actions)
	check("jump's defaults", InputBindings.get_bindings("jump") == [{"key": "Space"}, {"device": "gamepad", "button": "a"}], InputBindings.get_bindings("jump"))
	check("an axis default keeps the action's deadzone", InputBindings.get_bindings("move_left")[1] == {"device": "gamepad", "axis": "left_x", "from": 0.0, "to": -1.0, "deadzone": 0.2}, InputBindings.get_bindings("move_left"))

	key(KEY_SPACE, true)
	await process_frame
	check("a key presses its action", InputBindings.is_pressed("jump") and Input.is_action_pressed("jump"))
	check("just pressed, the frame it was", InputBindings.is_just_pressed("jump"))
	await process_frame
	check("not just pressed the frame after", not InputBindings.is_just_pressed("jump") and InputBindings.is_pressed("jump"))
	key(KEY_SPACE, false)
	await process_frame
	check("let go", not InputBindings.is_pressed("jump") and InputBindings.is_just_released("jump"))

	# Presses the game makes itself are player 1's.
	Input.action_press("jump")
	await process_frame
	check("Input.action_press() presses for player 1", InputBindings.is_pressed("jump") and InputBindings.is_just_pressed("jump"))
	check("not for player 2", not InputBindings.is_pressed("jump", 1))
	Input.action_release("jump")
	await process_frame
	check("and lets go", not InputBindings.is_pressed("jump") and InputBindings.is_just_released("jump"))
	var pressed_action := InputEventAction.new()
	pressed_action.action = &"jump"
	pressed_action.pressed = true
	Input.parse_input_event(pressed_action)
	await process_frame
	check("an InputEventAction presses for player 1", InputBindings.is_pressed("jump") and InputBindings.is_just_pressed("jump"))
	var released_action := InputEventAction.new()
	released_action.action = &"jump"
	Input.parse_input_event(released_action)
	await process_frame
	check("and its release lets go", not InputBindings.is_pressed("jump"))

	# Read from physics, a press is just pressed for one physics tick, however ticks and frames fall.
	key(KEY_SPACE, true)
	await physics_frame
	while not InputBindings.is_pressed("jump"):
		await physics_frame
	check("just pressed in physics, the tick it was", InputBindings.is_just_pressed("jump"))
	await physics_frame
	check("not the tick after", not InputBindings.is_just_pressed("jump"))
	key(KEY_SPACE, false)
	await physics_frame
	check("let go in physics", InputBindings.is_just_released("jump"))
	key(KEY_SPACE, true)
	key(KEY_SPACE, false)
	await physics_frame
	check("a tap shorter than a tick still seen in physics", InputBindings.is_just_pressed("jump") and InputBindings.is_just_released("jump"))
	await process_frame

	mouse(MOUSE_BUTTON_LEFT, true)
	await process_frame
	check("a mouse button presses its action", InputBindings.is_pressed("fire") and Input.is_action_pressed("fire"))
	mouse(MOUSE_BUTTON_LEFT, false)

	key(KEY_D, true)
	await process_frame
	check("axes and vectors from keys", InputBindings.get_axis("move_left", "move_right") == 1.0 and InputBindings.get_vector("move_left", "move_right", "jump", "fire") == Vector2(1, 0))
	key(KEY_D, false)

	key(KEY_H, true)
	await process_frame
	check("a context's bindings do nothing until it's on", not InputBindings.is_pressed("honk"))
	key(KEY_H, false)
	InputBindings.set_context_active("driving", true)
	key(KEY_H, true)
	await process_frame
	check("and work once it is", InputBindings.is_pressed("honk") and Input.is_action_pressed("honk") and InputBindings.is_context_active("driving"))
	key(KEY_H, false)
	InputBindings.set_context_active("driving", false)

	InputBindings.bind("jump", {"key": "J"})
	check("a binding added is saved", FileAccess.file_exists("user://input_bindings/default.json") and InputBindings.is_customised())
	key(KEY_J, true)
	await process_frame
	check("and works", InputBindings.is_pressed("jump") and Input.is_action_pressed("jump"))
	key(KEY_J, false)
	check("the actions using an input", InputBindings.get_actions_using({"key": "J"}) == PackedStringArray(["jump"]))
	InputBindings.unbind("jump", {"key": "J"})
	check("and removed", InputBindings.get_bindings("jump").size() == 2)
	InputBindings.reset()
	check("reset to the defaults, the saved file gone", not InputBindings.is_customised() and not FileAccess.file_exists("user://input_bindings/default.json"))

	InputBindings.bind("honk", {"key": "G"}, 0, "driving")
	check("a context's bindings changed on their own", InputBindings.get_bindings("honk", 0, "driving") == [{"key": "H"}, {"key": "G"}] and InputBindings.get_bindings("honk").is_empty())
	InputBindings.set_context_active("driving", true)
	check("what applies now includes active contexts", InputBindings.get_active_bindings("honk") == [{"key": "H"}, {"key": "G"}])
	InputBindings.set_context_active("driving", false)
	var exported := InputBindings.export_bindings()
	InputBindings.reset()
	InputBindings.import_bindings(exported)
	check("a whole set exported and imported", InputBindings.get_bindings("honk", 0, "driving").size() == 2 and InputBindings.is_customised())
	var events := InputBindings.get_binding_events({"key": "G"})
	check("a binding's input events", events.size() == 1 and events[0] is InputEventKey and events[0].physical_keycode == KEY_G)
	InputBindings.reset()

	# Player 2 takes the keyboard, with bindings of their own.
	InputBindings.set_player_devices(1, PackedInt32Array([InputBindings.KEYBOARD]))
	InputBindings.set_player_profile(1, "guest")
	InputBindings.bind("jump", {"key": "K"}, 1)
	key(KEY_K, true)
	await process_frame
	check("player 2's own binding works for them", InputBindings.is_pressed("jump", 1))
	check("and not for player 1, nor in the Input Map", not InputBindings.is_pressed("jump", 0) and not Input.is_action_pressed("jump"))
	check("the keyboard's player", InputBindings.get_device_player(InputBindings.KEYBOARD) == 1)
	key(KEY_K, false)
	check("player 1's bindings are untouched", InputBindings.get_bindings("jump", 0).size() == 2)
	InputBindings.reset(1)
	InputBindings.set_player_devices(1, PackedInt32Array())
	check("the keyboard back with player 1", InputBindings.get_device_player(InputBindings.KEYBOARD) == 0)
	check("and every controller nobody has", InputBindings.get_device_player(5) == 0)
	InputBindings.set_player_devices(0, PackedInt32Array([InputBindings.KEYBOARD, 3]))
	check("player 1 given their own keeps just those", InputBindings.get_device_player(3) == 0 and InputBindings.get_device_player(5) == -1)
	check("and the keyboard", InputBindings.get_device_player(InputBindings.KEYBOARD) == 0 and InputBindings.get_player_devices(0).has(InputBindings.KEYBOARD))
	InputBindings.set_player_devices(0, PackedInt32Array())
	check("an empty list gives player 1 everything again", InputBindings.get_device_player(5) == 0)

	var pad := InputEventJoypadButton.new()
	pad.device = 7
	pad.button_index = JOY_BUTTON_A
	pad.pressed = true
	Input.parse_input_event(pad)
	await process_frame
	check("the controller player 1 used last", InputBindings.get_player_last_device(0) == 7)
	var moved := InputEventMouseMotion.new()
	moved.relative = Vector2(12, 0)
	Input.parse_input_event(moved)
	await process_frame
	check("moving the mouse counts as using it", InputBindings.get_player_last_device(0) == InputBindings.KEYBOARD)
	pad.pressed = false
	Input.parse_input_event(pad)

	# Player 2 joins with a spare controller: the press was player 1's until the controller was theirs.
	for device in [4, 9]:
		for pressed in [true, false]:
			var each := InputEventJoypadButton.new()
			each.device = device
			each.button_index = JOY_BUTTON_A
			each.pressed = pressed
			Input.parse_input_event(each)
	await process_frame
	InputBindings.set_player_devices(1, PackedInt32Array([9]))
	InputBindings.set_player_devices(0, PackedInt32Array([InputBindings.KEYBOARD, 4]))
	check("player 1 goes back to the controller they used before", InputBindings.get_player_last_device(0) == 4)
	check("and player 2 starts on theirs", InputBindings.get_player_last_device(1) == 9)
	InputBindings.set_player_devices(1, PackedInt32Array())
	InputBindings.set_player_devices(0, PackedInt32Array())

	var capturing := InputBindings.capture()
	key(KEY_Q, true)
	var captured: Dictionary = await capturing
	key(KEY_Q, false)
	check("capturing a key", captured.ok and captured.binding == {"key": "Q"}, captured)
	capturing = InputBindings.capture()
	mouse(MOUSE_BUTTON_RIGHT, true)
	captured = await capturing
	mouse(MOUSE_BUTTON_RIGHT, false)
	check("capturing a mouse button", captured.ok and captured.binding == {"mouse": "right"}, captured)
	capturing = InputBindings.capture()
	key(KEY_ESCAPE, true)
	captured = await capturing
	key(KEY_ESCAPE, false)
	check("escape cancels", not captured.ok and captured.error == "cancelled", captured)

	check("describing", InputBindings.describe("jump") == "Space" and InputBindings.describe_binding({"mouse": "left"}) == "Left mouse button" and InputBindings.describe_binding({"device": "gamepad", "axis": "left_x", "to": -1.0}) == "Left Stick Left", [InputBindings.describe("jump"), InputBindings.describe_binding({"device": "gamepad", "axis": "left_x", "to": -1.0})])

	var space := InputBindings.get_prompt("jump")
	check("the game's own image wins", space != null and space.get_size() == Vector2(7, 7), space)
	var w := InputBindings.get_prompt("accelerate")
	check("a key's prompt, at the project's size", w != null and w.get_size() == Vector2(64, 64), w)
	check("at another size", InputBindings.get_prompt("accelerate", 0, 32).get_size() == Vector2(32, 32))
	check("a mouse prompt", InputBindings.get_binding_prompt({"mouse": "left"}) != null)
	check("a controller's prompt, with none plugged in, as Xbox's", InputBindings.get_binding_prompt({"device": "gamepad", "button": "a"}) != null)
	check("the keyboard's family", InputBindings.get_player_family(0) == InputBindings.FAMILY_KEYBOARD and InputBindings.get_device_family(InputBindings.KEYBOARD) == InputBindings.FAMILY_KEYBOARD)

	ProjectSettings.set_setting("input_devices/prompts/style", "pixel")
	var pixel_w := InputBindings.get_prompt("accelerate")
	check("pixel prompts are 16 by 16", pixel_w != null and pixel_w.get_size() == Vector2(16, 16), pixel_w)
	var pixel_shift := InputBindings.get_binding_prompt({"key": "Shift"})
	check("a wide key spans tiles", pixel_shift != null and pixel_shift.get_size() == Vector2(32, 16), pixel_shift)
	ProjectSettings.set_setting("input_devices/prompts/style", "standard")

	var label := RichTextLabel.new()
	root.add_child(label)
	InputBindings.show_prompts_in(label, "Press {accelerate} to go, {nothing} stays")
	check("text with prompts in it", label.get_parsed_text().begins_with("Press ") and label.get_parsed_text().ends_with(" to go, {nothing} stays"), label.get_parsed_text())

	var prompt := InputPrompt.new()
	prompt.action = &"accelerate"
	root.add_child(prompt)
	check("a prompt node sizes to its image", prompt.get_combined_minimum_size() == Vector2(64, 64), prompt.get_combined_minimum_size())
	prompt.height = 24
	check("or to its height", prompt.get_combined_minimum_size() == Vector2(24, 24), prompt.get_combined_minimum_size())

	print("controls test: ", "all checks passed" if failures == 0 else "%d failed" % failures)
	quit(failures)
