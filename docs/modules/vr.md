# VR

Games use Godot's OpenXR and the usual XR nodes, the same code on every platform. On Windows and Linux, OpenXR uses the player's own runtime, such as SteamVR or Meta's. On a Mac, where headset makers ship none, Purr has its own runtime, which shows the game in whichever of these headsets turns up, with nothing for the player to choose:

- Meta Quest and other headsets with Valve's Steam Link app, on the same network. The Mac encodes the game and streams it.
- Apple Vision Pro, on macOS 26 or later. macOS draws the game for the Vision Pro itself.

Turn on `xr/openxr/enabled` and `xr/shaders/enabled` in Project Settings, then start XR as usual:

```gdscript
func _ready() -> void:
	if VR.is_running():
		get_viewport().use_xr = true
```

## Turning VR on and off

On macOS, Windows and Linux, the player decides whether the game starts in a headset. It starts on a screen until they choose otherwise. Offer the choice in the game's settings, only where it applies:

```gdscript
vr_option.visible = VR.can_choose()
vr_option.button_pressed = VR.enabled

func _on_vr_option_toggled(on: bool) -> void:
	VR.enabled = on  # saved, and used from the next start
	VR.restart()     # or let it wait until the player next starts the game
```

`VR.can_choose()` is true when the game can play in a headset there (Purr's runtime on a Mac; an OpenXR runtime of the player's own, such as SteamVR, on Windows and Linux) and the launch didn't decide. OpenXR starts with the game, so a change takes effect when it next starts.

A launch can decide instead, and the option is then hidden:

- `-openxr` starts the game in a headset.
- `-novr` keeps it on the screen (and wins if both are given).
- Godot's own `--xr-mode on` and `--xr-mode off` do the same.

On Steam, add a launch option with `-openxr` as "Launch in VR" and, if you like, one with `-novr` for the desktop. Limit the `-novr` one to Windows and Linux: there's no SteamVR on a Mac, so Mac players keep the choice in the game.

Other platforms build for a headset or they don't, and OpenXR starts as the project says.

macOS exports include the runtime when the export preset's **Include VR Runtime** option (`xr/include_vr_runtime`) is on. It's on by default when `xr/openxr/enabled` is, so a game without VR carries none of it. A game that turns OpenXR on as it starts, through a settings override or `--xr-mode`, leaves `xr/openxr/enabled` off in its project, so turn the option on in its macOS preset. A player can still pick another runtime with the `XR_RUNTIME_JSON` environment variable.

## Steam Link

The player opens Steam Link on the headset. The Mac shows up in its list as the Mac's name followed by "(VR)". The first time, the headset shows a PIN and the game asks for it in a small sheet; after that, every game made with Purr on that Mac is paired. When VR starts in the game, the picture appears in the headset, with the game's sound.

The first time a player starts VR, macOS asks them to allow the game on the local network (to find headsets) and to record its own audio (the Mac sends only the game's sound). Exports add the explanations macOS shows, naming the game, unless the export preset already sets them, and turn on what the game needs to be signed with: the audio input entitlement and, in the App Sandbox, network access both ways.

The headset's controllers reach the game as OpenXR's Meta Touch controller (`/interaction_profiles/oculus/touch_controller`). Bind actions for it in the action map, as Godot's default one does, and use `XRController3D` as usual. Haptics work too. A game that's played with a pad, a wheel or the keyboard can turn controllers off with `xr/controllers`.

When the player recentres on the headset (holding its system button), the game's `LOCAL` space moves to where they are and Godot's `OpenXRInterface` emits `pose_recentered`, as with any OpenXR runtime.

## Apple Vision Pro

When a Vision Pro is on the same network while VR runs, the game asks macOS to show it there: macOS asks which Vision Pro to use, and the player accepts on the headset. While it shows the game, Steam Link steps aside. A player who closes or declines it isn't asked again until the Vision Pro leaves the network or VR starts again. Without a Vision Pro around, nothing is asked. The Vision Pro tracks the head, but macOS doesn't pass on hands or controllers from a remote headset, so a Vision Pro suits games played with a pad, a wheel or the keyboard. The game's sound plays on the Mac.

## The player's choices on a Mac

On a Mac, `VR` also holds the player's choices for the headset, saved with the game's other data and used when VR next starts: the size each eye is drawn at (`eye_scale`), and for Steam Link the refresh rate, the bitrate and whether the Mac goes quiet while the headset plays the sound. A Vision Pro always runs at 90 Hz. See [VR](../reference/VR.md).

Drawing both eyes costs about the same as drawing them one at a time, with any anti-aliasing or upscaling (FSR, FSR 2, MetalFX), in Forward+ and Mobile.

## Limits

- The aim pose is the same as the grip pose for now, and the palm pose (`grip_surface`) isn't tracked.

The runtime's log is in the game's data folder, under `logs/vr.log`, and the Steam Link streamer's is `vr/streamer.log`. The streamer also writes its state to its standard output, one JSON object per line (`waiting`, `connected`, `stats`, `vision_pro` and the PIN events, listed in `streamer/src/status.rs`), for a program that starts it to show. A program that starts the streamer itself sets `PURR_VR_STREAMER=external` for the game, so the runtime doesn't start another. `samples/vr_streamer` starts XR in a small scene: run it with `-openxr`.
