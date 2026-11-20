# Cameras

`VirtualCamera3D` and `VirtualCamera2D` are places for the scene's camera to be. Put as many as you like in a scene beside an ordinary `Camera3D` or `Camera2D`. Each one works out every frame where it should be (following a target, orbiting it, sliding along a rail, framing a group), and the real camera takes the place of the one with the highest priority. Change priorities and the camera blends from one to the next.

```gdscript
func _on_boss_appeared() -> void:
	$BossCamera.priority = 10


func _on_boss_defeated() -> void:
	$BossCamera.priority = 0
```

There's nothing else to set up: the real camera is the viewport's current camera, as usual.

## Choosing the live camera

The live camera is the one with the highest `priority` in its viewport. On a tie, the one that entered the tree or changed priority last wins, so adding a camera with the same priority as the live one takes over. A camera that can't process (with `process_mode` disabled, or pausable while the game is paused) doesn't compete. `is_live()` says whether a camera is the live one, and `live_changed` is emitted when that changes.

When the live camera changes, the real camera blends from where it was over the new camera's `blend_time`, eased by `blend_transition` and `blend_ease` as a `Tween` would. A `blend_time` of `0` cuts.

## Following

`follow_mode` says how a camera moves:

- `FOLLOW_NONE` stays where you put it, for a fixed camera. It can still turn to face `look_at_target`.
- `FOLLOW_TARGET` keeps `follow_offset` from `follow_target`, turned the way you turned it: an overhead or side-on camera.
- `FOLLOW_ORBIT` circles `follow_target` at `orbit_distance`, `orbit_pitch` and `orbit_yaw`, facing the point `follow_offset` above it: a third-person camera. Set the pitch and yaw from the player's input to let them look round. When something on `orbit_collision_mask` comes between the camera and its target, the camera moves in front of it at once, and eases back out.
- `FOLLOW_PATH` stays on the `Path3D` in `follow_path`, at the point closest to its target, facing it: a camera on a rail.
- `FOLLOW_GROUP` keeps every node in `follow_targets` in view. In 3D it backs off along `follow_offset` as they spread out; in 2D it zooms out, keeping `group_margin` pixels round them.

`follow_damping` makes a camera ease after its target rather than keep up exactly, and `look_damping` does the same for turning. Both are roughly the seconds it takes to cover most of the way. After teleporting a target, call `snap()` so the camera jumps with it.

A 3D camera follows where its target is drawn, so a target moved in `_physics_process` with physics interpolation on is followed smoothly between ticks.

In VR the headset moves the camera, so virtual cameras leave an `XRCamera3D` alone.

## Shaking

```gdscript
func _on_explosion(strength: float) -> void:
	$Camera.shake(strength, 0.5)
```

`shake()` turns a 3D camera by up to a few degrees, or moves a 2D one by a few pixels on screen, at a strength of `1.0`, fading out over the duration. A weaker shake doesn't cut short a stronger one.

## Focus

`focus_mode` gives a 3D camera depth of field that keeps something in focus, as a camera's autofocus does: what's in the middle of the view (`FOCUS_CENTER`, the first thing on `focus_collision_mask` straight ahead), or its target (`FOCUS_TARGET`, the `look_at_target` or else the `follow_target`). The focus eases to a new distance over `focus_damping`, and `focus_blur` sets how blurred the rest is. The real camera's attributes take the focus while the camera is live, and are given back when it isn't.

## Keeping the player in sight

With `fade_obstacles`, a following camera fades out what stands between it and its target (a wall, a tree, a pillar) so the player stays in sight, and fades it back once it's out of the way. What counts is any body on `orbit_collision_mask` in the way: the meshes under it, and the mesh it's under. `fade_amount` is how see-through they become.

## 2D

`VirtualCamera2D` has the target and group modes, with `zoom` for the real camera's zoom while it's live (in group mode, the closest it zooms in). The real `Camera2D`'s position smoothing is turned off while it's driven, as `follow_damping` does that job. Its limits still apply.

## Photo mode

Players like to stop the game and take a picture of it. Call `PhotoMode.open()` from your pause menu:

```gdscript
func _on_photo_mode_pressed() -> void:
	PhotoMode.open()
```

The game stops, its HUD goes, and the player flies a camera round the scene from where yours was. A small panel sets the field of view, roll, focus distance, background blur and exposure, adds a vignette or film grain, and takes the photo, without the panel in it. Photos are saved as PNG files in `user://photos` (`photo_mode/photos_folder` in Project Settings), and `photo_taken` says where.

The camera sees as yours did, with its environment and compositor effects, and stays within 20 m of where it started (`photo_mode/max_distance`), so players can't fly out of the level to see what isn't finished. It works with a mouse and keyboard (hold the right button to look, WASD to move, H hides the panel) and with a controller (the sticks to move and look, A takes a photo, B closes). `PhotoMode.close()`, or Escape, gives the game back as it was.

