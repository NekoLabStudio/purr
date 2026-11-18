# Time of day

`TimeOfDay` moves a sun and a moon across the sky as the hours pass. Add one to a 3D scene with two `DirectionalLight3D` children: the first is the sun, the second the moon.

```
Level
├── WorldEnvironment
└── TimeOfDay
    ├── Sun (DirectionalLight3D)
    └── Moon (DirectionalLight3D)
```

As `time` passes, the sun rises in the east, crosses the sky and sets, low and warm at either end and white at noon. At night the moon takes over with a dim blue light, and the world's sky and ambient light dim to `night_sky_energy` of their own. Whichever light is below the horizon is turned off, so its shadows cost nothing.

`time` is in hours, from `0` (midnight) to `24`, and `day_length` is how many real seconds a whole day takes: 20 minutes by default. Set it to `0` to stop the clock, for a level that's always at dusk. In the editor, drag `time` to see any hour; the clock only runs in the game.

The sun rises in the node's +X direction, so turn the `TimeOfDay` to set where east is. `latitude` sets how high the sun gets: near the equator it passes overhead, far north it stays low.

## In the game

```gdscript
func _ready() -> void:
	$TimeOfDay.hour_changed.connect(_on_hour_changed)


func _on_hour_changed(hour: int) -> void:
	if hour == 20:
		$Town.light_street_lamps()
```

`is_day()` says whether the sun is up, and `get_daylight()` how light it is, from `0` at night to `1` by day, easing in between round sunrise and sunset: handy for fading in lamps or changing music.
