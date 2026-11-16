extends HBoxContainer
# Two local players side by side, each with their own menu in their own SubViewport. With
# local_focus on, each viewport keeps its own focused button: the left player moves with W and S,
# the right player with the arrow keys, and neither takes the other's focus.

const KEYS := [[KEY_W, KEY_S], [KEY_UP, KEY_DOWN]]

var viewports: Array[SubViewport] = []


func _ready() -> void:
	for player in 2:
		var container := SubViewportContainer.new()
		container.stretch = true
		container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		add_child(container)
		var viewport := SubViewport.new()
		viewport.local_focus = true
		container.add_child(viewport)
		var menu := VBoxContainer.new()
		menu.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
		viewport.add_child(menu)
		for item in ["Play", "Character", "Options"]:
			var button := Button.new()
			button.text = "Player %d: %s" % [player + 1, item]
			menu.add_child(button)
		menu.get_child(0).grab_focus.call_deferred()
		viewports.append(viewport)


# Each player's keys go to their own viewport only, as a game routes each player's pad.
func _unhandled_key_input(event: InputEvent) -> void:
	for player in 2:
		var keys: Array = KEYS[player]
		if event.keycode in keys:
			var action := InputEventAction.new()
			action.action = "ui_up" if event.keycode == keys[0] else "ui_down"
			action.pressed = event.pressed
			viewports[player].push_input(action)
			get_viewport().set_input_as_handled()
