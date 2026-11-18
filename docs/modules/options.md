# Options

`Options` keeps the player's settings: what an options menu changes. Each has a key, a default and a current value. Changes are saved for the next run, and the engine's own take effect as soon as they're set.

```gdscript
$MusicSlider.value = Options.get_value("audio/music_volume")
$MusicSlider.value_changed.connect(func(value): Options.set_value("audio/music_volume", value))
```

## Built in

| Key | Value |
| --- | --- |
| `display/window_mode` | `"windowed"`, `"fullscreen"` or `"exclusive_fullscreen"` |
| `display/vsync` | `true` or `false` |
| `display/max_fps` | 0 (no cap) to 1000 |
| `privacy/send_crash_reports` | `true` once the player agrees to send [crash reports](crash-reports.md#sending-them); `false` until then |
| `display/graphics_tier` | `"ultra"`, `"high"`, `"medium"` or `"low"`: see [Graphics settings](/modules/graphics/) |
| `audio/<bus>_volume` | 0 to 1, one for each audio bus: `audio/master_volume`, `audio/music_volume` |

Their defaults come from Project Settings and the default bus layout, and the graphics tier's from the computer (`RecommendedGraphics.get_tier()`). If the window goes fullscreen or back another way (its own button, or the game's code), `display/window_mode` follows it, so the menu shows what the window is and the next run opens the same way. The window opens in the mode the player chose, with their vsync, frame rate cap and graphics tier, so a fullscreen player never sees it open windowed first.

## The game's own

Add a setting under `options/` in Project Settings, and it's an option, with the setting's value as its default: `options/game/show_hints` is the option `game/show_hints`. Keys always have a section and a name.

Or define one in code, before it's used. `define()` also takes the option's range, or the values it may have:

```gdscript
Options.define("game/difficulty", "normal", { "choices": ["easy", "normal", "hard"] })
Options.define("game/mouse_sensitivity", 1.0, { "min": 0.1, "max": 5.0 })
```

Numbers are kept within the range, and a value that isn't one of the choices or has the wrong type is refused. `get_info()` returns the range or the choices, so a menu can build its controls from them.

Put the game's options into effect when the game starts, and as they change:

```gdscript
func _ready() -> void:
	$Hints.visible = Options.get_value("game/show_hints")
	Options.changed.connect(_on_option_changed)


func _on_option_changed(key: String, value: Variant) -> void:
	if key == "game/show_hints":
		$Hints.visible = value
```

## Resetting

`reset()` gives one option back its default, and `reset_all()` all of them, for a menu's "Reset to defaults".

## Where they're saved

In `user://options.cfg`, written within a second of a change, as the game goes to the background on a phone, tablet or Apple TV, and as it quits. Only the options the player changed are written, so when a game update changes a default, every player who never touched it gets the new one. A file that can't be read is renamed `options.bad.cfg` and the game starts with the defaults.

Options are the player's on this device: they aren't synced, since the right window mode or volume on one computer is rarely right on another. Controls are kept by [InputBindings](/modules/controls/), and the player's VR choice by [VR](/modules/vr/).

In the editor, options can be read but aren't saved or put into effect.
