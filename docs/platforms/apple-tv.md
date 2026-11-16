# Apple TV

Purr exports to tvOS. Godot doesn't yet; Purr carries the work in progress on adding it (Godot's [pull request 118332](https://github.com/godotengine/godot/pull/118332)) as patches, with a few additions of its own.

Install Purr's export templates, add a tvOS export preset and export an Xcode project. Archive and upload it from Xcode as with any Apple TV app.

## Input

The Siri Remote arrives as keys and a joypad, so Godot's default UI navigation works with it:

- The clickpad's edges (or D-pad) are the arrow keys, and its click is Enter.
- Play/Pause is the media play key (`KEY_MEDIAPLAY`).
- Where a finger rests on the touch surface is a joypad's left stick, on device -3, which actions bound to all devices hear.
- Menu goes back. By default it returns to the Home screen. With `get_tree().quit_on_go_back` off, the game gets `NOTIFICATION_WM_GO_BACK_REQUEST` and `KEY_MENU` instead, and decides what back means.

Game controllers work as on other platforms.

## Text input

Editing a `LineEdit` brings up the system keyboard. The `LineEdit`'s maximum length applies, and dismissing the keyboard with Menu stops editing.

## Saves

tvOS lets a game write only to its caches, so on Apple TV `user://` is the Caches folder, and tvOS can delete what's there whenever it needs space. Saves in [iCloud](apple.md#icloud-saves) or on the [online services](../online/index.md) come back from there, so set up one of them. Saves with no copy elsewhere (no iCloud, or a local profile without it) go in the game's preferences, which tvOS keeps, but which hold only about 500 KB for the whole game. Purr's export warns when neither iCloud nor the online services are set up.

An Apple TV can have several users. The export preset's Entitlements > User Management option decides how the game treats them. By default each user has their own saves and keychain, with a keychain the whole household shares beside them. Purr keeps remembered guests in the shared one, so a guest one user remembers is there for everyone.
