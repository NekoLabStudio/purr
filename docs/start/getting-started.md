# Getting started

Purr is NekoLab's game engine: Godot with what most games end up needing built in. Godot's own [documentation](https://docs.godotengine.org/en/latest/) covers the editor, scenes, scripting and everything else Purr doesn't change. These pages cover what Purr adds:

- Accounts, saves, achievements, leaderboards, lobbies, friends and local players, through one API that works the same on Steam, Game Center, Google Play Games and a device without a platform.
- Purr's own [online services](../online/index.md), which you run yourself, for crossplay, progress across platforms and ranked play.
- WebRTC, steering wheel force feedback, VR on a Mac, recommended graphics settings and Apple TV exports.

## Installing Purr

On a Mac, install Purr with Homebrew:

```sh
brew install --cask nekolabstudio/tap/purr
```

`brew upgrade` keeps it up to date. Otherwise, download the editor for macOS (Apple silicon), Windows or Linux from Purr's [releases](https://github.com/NekoLabStudio/purr/releases), unzip it and open it.

Then install the export templates, which every export needs: in the editor, go to Editor > Manage Export Templates and choose Download and Install. The templates cover macOS, Windows, Linux, iOS, Apple TV, Apple Vision Pro and Android. Without an internet connection, download the release's `.tpz` file elsewhere and choose Install from File. Templates only work with the editor of the same Purr version.

Purr keeps its settings, templates and project list apart from Godot's, so both can be installed side by side.

### The `purr` command

Scripts and tests run Purr as `purr`, never `godot`, so they always get Purr whatever else is installed:

```sh
purr --headless --path my_game -s tests/run.gd
```

- macOS: Homebrew adds `purr` to your path. With the downloaded app, link it yourself: `ln -s /Applications/Purr.app/Contents/MacOS/Purr /usr/local/bin/purr`.
- Linux: the editor is `purr`. Put its folder on your path.
- Windows: put the editor's folder on your path and run `Purr_console`, which prints to the terminal (`Purr.exe` is the editor without a console).

## Coming from Godot

Purr opens Godot projects of the Godot version it's based on (see the release notes). Four things work differently:

- There's no Compatibility (OpenGL) renderer. Purr runs on Vulkan, Direct3D 12 and Metal, so there are no web exports.
- 3D physics is always Jolt, including in projects that never chose a physics engine. 2D physics is Godot's.
- APIs Godot has deprecated are left out. If a script stops parsing, the editor names the replacement.
- New environments use the Parametric tonemapper, a film-like curve that keeps mid grey and rolls highlights off into white. Godot's default was Linear, so an environment that never chose a tonemapper looks different in Purr: set Tonemap > Mode back to Linear to keep the old look.

GDExtensions need to be built against Purr's API. Run the editor with `--headless --dump-extension-api` and build the extension against the `extension_api.json` it writes.

## Next

[Your first game](first-game.md) builds a small game that signs in, saves and unlocks an achievement, step by step. After that:

- [Accounts, saves and play together](../api/index.md) for what every game needs.
- The platform you're shipping on: [Steam](../platforms/steam.md), [Apple platforms](../platforms/apple.md), [Apple TV](../platforms/apple-tv.md) or [Android](../platforms/android.md).
- [Exporting](exporting.md) when you're ready to ship.

Every class Purr adds is in the class reference, here and in the editor (press F1 and search). The repository's `samples/` folder has a small project for each part of Purr, such as `samples/purr` for accounts, saves and lobbies, and `samples/force_feedback` for steering wheels.
