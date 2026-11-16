# Your first game

This page builds a small game that signs the player in, keeps a count of coins in their save and unlocks an achievement. It runs as it is on any computer, and the same code works on Steam, Game Center and Play Games once you add them.

## Make a project

Open Purr, choose Create in the Project Manager, name the project and keep the Forward+ renderer. Add a scene with a `Control` as its root, save it as `main.tscn` and make it the main scene (Project > Project Settings > Application > Run > Main Scene).

Add a `Label` and a `Button` to the scene, then attach this script to the root:

```gdscript
extends Control

var coins := 0


func _ready() -> void:
	await Account.sign_in()
	var saved = Saves.load("main")
	if saved is Dictionary:
		coins = saved.coins
	$Label.text = "%s has %d coins" % [Account.player_name, coins]
	$Button.pressed.connect(_on_button_pressed)


func _on_button_pressed() -> void:
	coins += 1
	$Label.text = "%s has %d coins" % [Account.player_name, coins]
	Saves.save("main", {"coins": coins}, {"coins": coins})
	if coins == 10:
		Achievements.unlock("ten_coins")
```

Signing in comes first: saves, achievements and lobbies all belong to the player who signed in. `Saves.save()` writes to disk at once, and `Saves.load()` reads it back the next time the game starts. The last argument to `save()` is a small summary the player sees if two of their devices ever disagree.

## Add the achievement

Go to Project > Project Settings and open the Achievements tab. Type `ten_coins` and choose Add. The row has a column for each platform's name for the achievement; leave them empty for now. An empty name means the platform uses the same ID.

## Run it

Press F5. Without a platform, the player signs in with an account for this device, called "Player" until they choose a name ([`Account.needs_display_name()`](../api/index.md#the-players-name) says when to ask). Click the button a few times, close the game and run it again: the coins are still there.

The achievement is kept with the player's save. When the game later runs on Steam, Game Center or Play Games, Purr unlocks it there too, so progress made before a platform was set up isn't lost.

## Add a platform

Each platform needs a little setup outside the code:

- [Steam](../platforms/steam.md): the game's App ID in Project Settings, and its achievements in Steamworks.
- [Game Center and iCloud](../platforms/apple.md): Game Center turned on in App Store Connect, and an iCloud container for saves.
- [Google Play Games](../platforms/android.md): the game's application ID, and Play Games Services in the Play Console.

Then give each achievement its platform name in the Achievements tab. The game's code stays as it is.

## Where next

- [Saves](../api/saves.md) explains where saves go and what to do when two devices disagree.
- [Lobbies](../api/lobbies.md) turns the game into a multiplayer one with a lobby code.
- The [online services](../online/index.md) add crossplay and progress that follows the player to every platform, by email.
- [Exporting](exporting.md) builds the game for each platform.
