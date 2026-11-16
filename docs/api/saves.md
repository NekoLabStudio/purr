# Saves

```gdscript
Saves.save("slot1", {"level": 4, "coins": 120}, {"level": 4, "playtime": 7200})
var data = Saves.load("slot1")
```

A save is any value Godot can store (dictionaries, arrays, numbers, strings, vectors), under a slot name of 1 to 64 lowercase letters, digits, `-` or `_` (so every cloud can store it). The last argument is a small summary, shown to the player if they ever have to choose between two saves, so put in what helps them recognise one: the level, the playtime, where they were.

Saves are local first. `Saves.save()` writes to disk at once and syncs in the background, so the game doesn't wait for the network and works offline. `Saves.load()` reads the copy on this device, at once. A guest's saves work the same way, through their own `saves`.

## Where saves go

Each player's saves go to one place:

1. The online services, once the player has [added an email](email.md).
2. Otherwise the platform's cloud: Steam Cloud on Steam, iCloud on Apple platforms (when the game [sets it up](../platforms/apple.md#icloud-saves)), Play Games' saved games on Android (when the game [turns them on](../platforms/android.md#saved-games)).
3. Otherwise this device only.

`saves/backend` can narrow this down. `automatic`, the default, follows the order above. `online` uses the online services or nothing, `platform` the platform's cloud or nothing, and `local` keeps saves on the device.

When a player adds an email, Purr uploads their saves on this device to their account. The game doesn't need to do anything.

## Two devices, two saves

When another device saved a slot since this one last synced, and this one has changes of its own, Purr doesn't pick one. `Saves` emits `conflict` with both summaries, and the game asks the player:

```gdscript
func _ready() -> void:
	Saves.conflict.connect(_on_save_conflict)


func _on_save_conflict(slot: String, this_device: Dictionary, other_device: Dictionary) -> void:
	var keep_this := await ask_player(this_device, other_device)
	Saves.resolve_conflict(slot, keep_this)
```

`Saves.synced` fires when a slot has reached the cloud or come down from it. Load the slot again then if the game is showing it.

## Limits

The online services keep up to 8 slots per player per game, of 1 MB each, unless the service sets other limits for the game. Steam Cloud's limits are the ones set for the game in Steamworks.
