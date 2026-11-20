# Water

`Water3D` is a body of water. Give it a `size` for a lake or a pond, or leave the size at zero for an endless sea round the camera that reaches the horizon. Its surface is at the node's height.

```
Level
├── TimeOfDay
├── Weather3D
└── Sea (Water3D)
```

A [terrain](terrain.md#ocean) has one built in: turn on its `ocean_enabled`.

It's drawn with a `WaterMaterial`, which you can also give to a mesh of your own: a river's ribbon, a fountain's basin. Its waves roll along the `Weather3D`'s wind and grow in a storm (`follow_wind`), or along `wave_direction` at `wave_height`. What's under the surface is seen through it, bent by the waves and fading into `deep_color` with depth: after `clarity` metres, what's below is `color` times as bright, so a pale blue-green keeps the blues and loses the reds. Foam gathers along shores and on steep crests, and rain ripples the surface. `flow` moves the surface along, for a river.

The waves stay put when the world shifts. For a boat or a swimmer, `get_height_at()` gives the surface's height where they are, waves and all:

```gdscript
func _physics_process(delta: float) -> void:
	var surface := $Sea.get_height_at(global_position)
	if global_position.y < surface:
		apply_central_force(Vector3.UP * (surface - global_position.y) * buoyancy)
```

Water needs the Forward+ or Mobile renderer. It's drawn after what's under it, so things under the surface show through.
