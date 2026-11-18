# Debug tools

Two tools for seeing what a game is doing while it runs: `DebugDraw` draws shapes and values, and `Console` runs commands you add. Both are on in debug builds (running from the editor, or a debug export) and off in release exports unless you turn them on.

## Drawing

Call `DebugDraw` from anywhere, and the shape shows for that frame:

```gdscript
func _physics_process(delta: float) -> void:
	DebugDraw.arrow(global_position, global_position + velocity, Color.YELLOW)
	DebugDraw.sphere(target, 0.5, Color.RED)
	DebugDraw.text(global_position + Vector3.UP * 2, state_name)
	DebugDraw.show_value("speed", velocity.length())
```

Call it every frame for as long as you want the shape. To leave one behind instead, give it a duration in seconds:

```gdscript
func _on_hit(at: Vector3) -> void:
	DebugDraw.point(at, 0.3, Color.ORANGE, 2.0)
```

In 3D there are `line()`, `path()`, `arrow()`, `point()`, `box()` (a cube transformed by a `Transform3D`, so it can be rotated and stretched), `aabb()`, `sphere()`, `axes()` and `text()`. Lines draw through what's in front of them faintly, so a shape behind a wall still shows where it is. In 2D there are `line_2d()`, `path_2d()`, `arrow_2d()`, `rect_2d()`, `circle_2d()` and `text_2d()`, in the world's coordinates, so they move with the camera.

`show_value()` lists a name and a value in the top-left corner of the screen. Floats show three decimals.

Shapes drawn in `_physics_process` stay until the next physics tick, so they don't flicker when the game draws more frames than it runs ticks. Everything drawn in a frame goes to the GPU as one mesh, so thousands of lines are fine. `DebugDraw.clear()` removes everything, including shapes with a duration.

Positions are global. With a floating origin (`WorldOrigin`), shapes with a duration move with the world when it shifts.

## Performance overlay

`DebugDraw.performance_overlay = true` (or `perf` in the [console](#console)) shows a panel in the top-right corner of the screen: a graph of the last couple of seconds of frame times, the frame rate, where the CPU and GPU spend the frame, the GPU's five costliest render passes, and the draw calls, objects, primitives and video memory. The numbers are averaged over half a second so you can read them; the CPU's process and physics times are the slowest of the last second, as the engine keeps them.

It says first whether a frame is held up by the CPU or the GPU, and then which pass. Measuring the GPU's passes costs a little, so it only happens while the overlay is shown.

## Profiling

`profile` in the [console](#console) records the next 10 seconds of play (or `profile 30` for 30), then saves a report to `user://profiles` and prints a summary. It works without the editor, so it's for a tester's computer, a phone, an Apple TV or a headset:

```
Frames: 598 in 10 s, 16.6 ms at the middle, 95% within 18.1 ms, slowest 61.0 ms; 2 took 40.0 ms or more.
The costliest script functions, each frame:
  2.31 ms  res://traffic.gd:212 _physics_process (598 calls)
  0.84 ms  res://crowd.gd:88 _update_people (598 calls)
```

The report also has the five longest frames, with what [FrameBudget](/modules/graphics/#long-frames) measured in them, and the 30 script functions that took longest, counting only their own time (not the functions they call), with the engine methods scripts called among them. For the GPU, use the performance overlay. Script functions are timed in debug builds only.

On a Mac, the editor and debug exports also mark the engine's own work for Instruments: each frame under Points of Interest, and the engine's steps (starting up, each frame's physics, drawing each viewport) as intervals in the os_signpost instrument. Record them beside a Metal System Trace to line the CPU's work up with the GPU's. Release exports leave the marks out.

## Console

The console opens with the key under Escape (backquote on most keyboards) and closes with it or Escape. It shows what the game prints, its warnings and its errors. Add commands for whatever helps you test:

```gdscript
func _ready() -> void:
	Console.add_command("teleport", teleport, "Moves the player to a point.")
	Console.add_command("god", set_invincible, "Turns damage off or on.")


func teleport(where: Vector3, height := 0.0) -> void:
	player.global_position = where + Vector3.UP * height


func set_invincible(on: bool) -> void:
	player.invincible = on
```

Typed in, `teleport 10,0,-4` calls `teleport(Vector3(10, 0, -4))`. Arguments are converted to the types the method declares: numbers, `true` or `false` (or `on` and `off`), text (in quotes when it has spaces), vectors (including whole-number ones) as `x,y` or `x,y,z`, and anything else written as in GDScript, such as `Color(1, 0, 0)`. Arguments with defaults can be left out. The console can't see a lambda's argument types, so a lambda gets numbers, `true` and `false`, or text: use a method when a command takes vectors or `on` and `off`. If a command returns something, the console shows it.

`help` lists every command with how to use it and what it does, `clear` empties the console, and `perf` shows or hides the [performance overlay](#performance-overlay). Up and Down go through earlier commands, and Tab completes a command's name.

`Console.run("teleport 0,0,0")` runs a command from code, which is handy in tests. `Console.write()` adds a line to the console without printing it to the log.

While the console is open, a captured or hidden mouse pointer is shown, and put back when it closes. What you type is the console's: asking `Input` whether a key or action is held gets no while it's open. A game's own `_input()` still sees the keys, so check `Console.visible` there if that matters. The game keeps running underneath, so pause it in a command if you need to.

## Project settings

| Setting | Default | What it does |
| --- | --- | --- |
| `debug_draw/enabled_in_release_builds` | off | Draws in release exports too. |
| `console/enabled_in_release_builds` | off | Lets the console open in release exports. |
| `console/open_key` | `QuoteLeft` | The key that opens and closes the console, by its position on the keyboard, named as Godot names keys (`F1`, `Tab`, `QuoteLeft`). |

Both can also be turned on and off as the game runs, with `DebugDraw.enabled` and `Console.enabled`: a game could open its console in a release build only when it's started with a launch option, for example.
