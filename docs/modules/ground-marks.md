# Ground marks

`GroundMarks3D` leaves marks on the ground where it goes: tyre marks, footprints, ski tracks. Put one where the marks start (a car's wheel, a foot) and turn `emitting` on while they should be left:

```gdscript
func _physics_process(delta: float) -> void:
	$RearLeftMarks.emitting = is_skidding()
```

Every `spacing` it moves, it lays a mark on the surface below it, found by a ray down `ground_distance` that doesn't count the body it's on. `MODE_STRIP` joins them into a strip, for tyres and skis; `MODE_STAMPS` leaves them separate, for footprints, each `length` long. Marks last `lifetime` seconds and fade out at the end of it, and at most `max_marks` are down at once. Off the ground, the strip breaks.

Without a `material`, the marks are plain and dark. For a tread or a footprint, give it a material with the mark as its texture, set to take its albedo from vertex colour so the marks fade. The marks stay put when the world shifts.
