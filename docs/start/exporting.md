# Exporting

Exporting works as in Godot, with Purr's export templates installed. Purr adds the files its modules need to each export.

On macOS, Windows and Linux, Purr adds Steam's library next to the game's executable. The engine is linked against it, so it has to be there even when the game doesn't use Steam, and it only connects to Steam when Steam is running.

On iOS and Apple TV, Purr adds the frameworks it uses to the Xcode project: Security (the keychain), GameKit (Game Center) and CloudKit (iCloud saves). On Android, with `play_games/app_id` set, it adds Play Games to Gradle builds. On macOS, in projects that use OpenXR, it adds Purr's VR runtime (see [VR](../modules/vr.md)).

Purr warns at export time in these cases:

- On Apple TV, when saves would only be kept on the device. tvOS can delete a game's files when it needs space, so set up [iCloud saves](../platforms/apple.md#icloud-saves) or the [online services](../online/index.md).
- On Apple platforms, when `icloud/container` is set but the export preset doesn't give the game that iCloud container.
- On Android, when `play_games/app_id` is set but the preset doesn't use the Gradle build.

See [Apple TV](../platforms/apple-tv.md) for exporting to tvOS.

## Headsets

A game can also run on a headset itself, as Godot games do:

- Meta Quest (and other Android headsets): export for Android with the Gradle build, with Godot's [OpenXR vendors plugin](https://github.com/GodotVR/godot_openxr_vendors/releases) in the project's `addons/` folder. Purr leaves out the Asset Library, so download the plugin from its releases.
- Apple Vision Pro: export for visionOS, as in Godot. Purr's templates include it.

Playing in a headset from a computer needs neither: see [VR](../modules/vr.md).
