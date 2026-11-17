# Controls

`InputBindings` lets players change their controls and keeps what they choose. It shows the right button prompts for whatever controller they're holding, and gives each local player their own devices. Your game's defaults stay where they already are: the Input Map in Project Settings.

## Reading the controls

Player 1's bindings go into Godot's Input Map, so the usual calls keep working:

```gdscript
if Input.is_action_just_pressed("jump"):
	jump()
```

`InputBindings` reads the same actions for any local player, with axes calibrated:

```gdscript
var steering := InputBindings.get_axis("steer_left", "steer_right", player)
if InputBindings.is_just_pressed("jump", player):
	jump()
```

`get_value()` gives how far an action is pressed (0 to 1), `is_pressed()`, `is_just_pressed()` and `is_just_released()` what you'd expect, and `get_axis()` and `get_vector()` directions from pairs of actions. Read from `_physics_process()`, "just pressed" lasts one physics tick, as with `Input`. Presses your game makes itself, with `Input.action_press()` or an `InputEventAction` (from on-screen buttons, say), count for player 1.

## Letting players change them

A controls screen asks for the next input and binds it:

```gdscript
func _on_rebind_pressed(action: StringName) -> void:
	var captured: Dictionary = await InputBindings.capture(player)
	if captured.ok:
		InputBindings.set_bindings(action, [captured.binding], player)
```

`capture()` waits for a key, mouse button, controller button, or an axis moved well away from where it rests (so a resting pedal isn't picked up by mistake). Escape cancels it. `bind()` and `unbind()` add and remove one binding, `get_bindings()` lists them, `get_actions_using()` says what else an input already does, and `reset()` goes back to your defaults. Every change is saved at once, in the user folder; set `InputBindings.save_folder` to keep them with your game's own saves. `export_bindings()` and `import_bindings()` give and take a player's whole set, for presets or copying them between players.

A binding is a dictionary, saved as readable JSON:

| Binding | Means |
|---|---|
| `{"key": "W"}` | a key, by where it is on the keyboard |
| `{"mouse": "left"}` | a mouse button or the wheel (`wheel_up`, `wheel_down`) |
| `{"device": "gamepad", "button": "a"}` | a controller button, by Godot's name (`a` is the bottom face button) or number |
| `{"device": "gamepad", "axis": "trigger_right", "from": 0.0, "to": 1.0, "deadzone": 0.05, "curve": 1.5}` | an axis, calibrated |
| `{"device": "Logitech G923", "hat": "up"}` | a steering wheel's hat |

`"gamepad"` means any controller. A binding made by capturing names the controller it came from, so it follows that controller to the next session and to another port. An axis goes from its `"from"` value to its `"to"` value, which covers its range and which way it goes. It ignores the first `"deadzone"` of its travel, and a `"curve"` above 1 makes it gentler at first.

## Defaults with more detail

The Input Map can't calibrate an axis or say a binding only applies sometimes. For those, add a defaults file and point `input_devices/bindings/defaults` at it:

```json
{
	"bindings": {
		"throttle": [{"key": "W"}, {"device": "gamepad", "axis": "trigger_right", "from": 0.0, "to": 1.0, "curve": 1.5}]
	},
	"contexts": {
		"manual": {
			"clutch": [{"key": "Shift"}, {"device": "gamepad", "button": "left_shoulder"}]
		}
	}
}
```

Actions in the file add to the Input Map's, or replace them. A context's bindings only apply while the game says so: `InputBindings.set_context_active("manual", true)` when the player gets into a manual car. A controls screen edits a context's bindings by naming it: `InputBindings.bind("clutch", binding, player, "manual")`.

## Button prompts

`InputPrompt` shows what to press for an action, on the controller the player is using. It switches as they pick up another one:

```gdscript
var prompt := InputPrompt.new()
prompt.action = &"jump"
add_child(prompt)
```

In text, `show_prompts_in()` turns each `{action}` into its prompt:

```gdscript
InputBindings.show_prompts_in($Hint, "Press {jump} to jump", player)
```

`get_prompt()` gives the image itself, and `describe()` the name of what to press ("Space", "Right Shoulder").

Purr tells controllers apart by their maker and model: Xbox, PlayStation 4 and 5, Nintendo, Steam Deck, steering wheels and others. Prompts show what's printed on the button, so the bottom face button shows a cross on a PlayStation pad and B on a Nintendo one. On Steam with Steam Input, Purr asks Steam what the player is really holding. Steering wheels Purr knows (Logitech's G29, G920 and G923 so far) name their own buttons, such as "Right paddle" or "Gear 3", with icons where a button has a symbol on it.

The images are Kenney's Input Prompts, in two styles set by `input_devices/prompts/style`:

- `standard`: crisp vector images, drawn at `input_devices/prompts/size` pixels (64 by default) or the size you ask for.
- `pixel`: 16 by 16 pixel art, for pixel art games. `InputPrompt` draws them with nearest filtering. Wide keys such as Shift and Space are wider.

To use your own images, make an `InputPromptSet`, give it images by control (`"xbox/a"`, `"playstation5/left_trigger"`, `"keyboard/space"`, `"mouse/left"`) and set `input_devices/prompts/custom_set` to it. Anything it leaves out shows Purr's.

## Local players

Player 1 has the keyboard, the mouse and every controller nobody else has. Give a local player their controller when they join:

```gdscript
func _input(event: InputEvent) -> void:
	if joining and event is InputEventJoypadButton and event.pressed:
		InputBindings.set_player_devices(next_player, PackedInt32Array([event.device]))
```

Once others have joined, you may want player 1 to keep only what they're playing with, so a spare controller can ask to join without moving them. Give player 1 their own devices too: from then on a controller nobody has belongs to nobody (`get_device_player()` gives -1). An empty list gives a player back what they have by default.

```gdscript
InputBindings.set_player_devices(0, PackedInt32Array([InputBindings.KEYBOARD, first_pad]))
```

Players share one set of bindings, `"default"`, until you give one its own with `set_player_profile()`. Using a profile ID, for example, means each profile keeps its own controls.

## Steering wheels

Wheels are never "gamepad": SDL reads some as gamepads, with their pedals as triggers that rest fully pressed. Bind a wheel's axes by capturing them, which names the wheel. Godot's own menu controls (`ui_up` and the rest) only answer controllers, so a wheel's pedals and steering don't move the highlight round your menus. A wheel's hat is told apart from its buttons with the same numbers, using [the force feedback module](force-feedback.md)'s `SteeringWheelHats`.

The class reference has everything: [InputBindings](../reference/InputBindings.md), [InputPrompt](../reference/InputPrompt.md) and [InputPromptSet](../reference/InputPromptSet.md). `samples/controls` shows what Purr makes of each controller you press, with its name and both prompt styles.
