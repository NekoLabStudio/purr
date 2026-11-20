# Time of day and weather

`TimeOfDay` moves a sun and a moon across the sky as the hours pass. Add one to a 3D scene with two `DirectionalLight3D` children: the first is the sun, the second the moon.

```
Level
├── WorldEnvironment
└── TimeOfDay
    ├── Sun (DirectionalLight3D)
    └── Moon (DirectionalLight3D)
```

As `time` passes, the sun rises in the east, crosses the sky and sets, low and warm at either end and white at noon. At night the moon takes over with a dim blue light, and the world's sky, ambient light and fog dim to `night_sky_energy` of their own. Whichever light is below the horizon is turned off, so its shadows cost nothing.

`time` is in hours, from `0` (midnight) to `24`, and `day_length` is how many real seconds a whole day takes: 20 minutes by default. Set it to `0` to stop the clock, for a level that's always at dusk. In the editor, drag `time` to see any hour; the clock only runs in the game.

The sun rises in the node's +X direction, so turn the `TimeOfDay` to set where east is. `latitude` sets how high the sun gets: near the equator it passes overhead, far north it stays low.

The moon goes through its phases as the days pass. A full moon rises as the sun sets and lights the night; a new moon sits beside the sun and gives no light, for a properly dark night. Set `moon_phase` (0 new, 0.5 full) for the night you want, and it moves on from there.

## In the game

```gdscript
func _ready() -> void:
	$TimeOfDay.hour_changed.connect(_on_hour_changed)


func _on_hour_changed(hour: int) -> void:
	if hour == 20:
		$Town.light_street_lamps()
```

`is_day()` says whether the sun is up, and `get_daylight()` how light it is, from `0` at night to `1` by day, easing in between round sunrise and sunset: handy for fading in lamps or changing music.

## Lights at dusk

Put street lamps, lit signs and glowing windows in the `night_lights` group and `TimeOfDay` switches them for you: they come on as it gets dark and go off in daylight. Each comes on at a slightly different moment, the same one every time, so a street's lamps light up one by one rather than all at once. Under heavy cloud they come on sooner. Anything can be in the group: a light, a mesh, a whole lamp post.

For windows with rooms behind them, give a building's front an `InteriorMaterial` (see [Large worlds](world.md#lights-at-night)): its rooms light up at night the same way.

## The sky

`CloudySkyMaterial` is a sky to go with it: give it to the `WorldEnvironment`'s `Sky`. Its colours move from night through dusk to day as the sun rises, with dusk's colour gathering on the sun's side, the sun's disc and glow, high thin cirrus and a layer of clouds lit from the sun's side. Each time of day has a top and a horizon colour to set, and `cloud_cover` says how much of the sky is cloud.

It follows the `TimeOfDay`'s sun even below the horizon, so dusk and night look right; without one, it follows the scene's first light. At night the stars and the Milky Way come out and turn with the sky through the night, and the moon shows its phase, lit from the side the sun is on.

### The sky as the air makes it

For a sky that looks real, use `AtmosphereSkyMaterial` instead. It works the sky out from the air itself: blue overhead and hazier towards the horizon, reddening at dawn and dusk as sunlight crosses more air, with the sun's disc coloured by the air in front of it. Add an `AerialPerspectiveEffect` to the environment's `Compositor` and the same air lies between the camera and the scene, so far hills turn pale and blue and a low sun's haze goes golden, meeting the sky at the horizon. Use it in place of the environment's fog.

Its clouds have depth: they're raymarched round the camera a sixteenth of the sky a frame, lit by the sun through the air, gathering into a low grey deck as the weather turns overcast and drifting with the wind (`volumetric_clouds`; in the editor, and with it off, painted clouds that cost less). `haze` and `haze_height` set how hazy the air is, and `brightness` how bright the sun's light is in your scene's units: match it to your exposure.

## Weather

`Weather3D` changes the clouds, rain, snow and fog gradually. Add one beside the `TimeOfDay` and set what you want:

```gdscript
$Weather3D.rain = 1.0         # a downpour
$Weather3D.lightning = 0.5    # with a storm
$Weather3D.cloud_cover = 0.2  # and a clearing sky afterwards
```

`cloud_cover`, `rain`, `snow` and `fog` are where the weather is heading: it eases there over `change_time` (30 seconds by default), with the sky clouding over before rain or snow falls and stopping before it clears. While the sky is overcast, the sun and moon dim and the sky turns grey. Rain falls round the camera, slanted by the `wind`, which also drifts the clouds, and splashes where it lands. Snow drifts down slowly, carried by the wind. Neither falls under roofs, bridges or trees: the weather keeps a height map of what's over the camera's surroundings, so a bus shelter keeps dry without any setting up.

The ground's wetness follows the rain: it soaks in `wetting_time` and dries in `drying_time`, so streets stay wet a while after a shower. Snow settles over `settling_time` and melts over `melting_time`, on whatever faces up: every lit `StandardMaterial3D`, terrain and `FoliageMaterial` plants, but not under roofs. Your own shader materials can have it too: see below. If your game doesn't want snow on its materials, turn off `rendering/weather/snow_on_materials`. `get_wetness()` and `get_snow_cover()` give them, and `settle()` jumps straight to the weather asked for, for the start of a level or after loading a save.

`fog` thickens the world's own fog: its volumetric fog if it has that on, otherwise its plain fog, given back as it was when the weather goes. Rain and snow bring a little haze of their own.

With `lightning` above 0, a storm (hard rain under overcast) has lightning now and then, lighting the clouds and the ground for a moment. `lightning_flashed` tells you how far away it struck, for the thunder:

```gdscript
func _on_lightning_flashed(distance: float) -> void:
	await get_tree().create_timer(distance / 343.0).timeout
	$Thunder.play()
```

### Wet surfaces

Add a `WetSurfacesEffect` to the environment's `Compositor` and the world gets wet with the rain, whatever its materials: what the rain reaches darkens and takes on a sheen, and flat ground gathers puddles that reflect what's above them and ripple while it rains. Metal and glass don't darken, walls get less wet than the ground, and what's under a roof stays dry. `darkening`, `puddles` and `reflections` set how much of each. It needs the Forward+ renderer, and reflects only what's on screen.

## Wind

Give grass, leaves and plants a `FoliageMaterial` and they sway in `Weather3D`'s wind: each copy leans with it, bends more the higher it is, catches gusts that roll across the field, and flutters on its own, so nothing moves in step. A storm's wind bends them hard; with no `Weather3D`, there's a light breeze. Set `sway_height` to how tall the plant is (a tree's crown, a blade of grass), and `stiffness` higher for branches than for grass. It's drawn from both sides, cut out by its texture, and lit through from behind like real leaves.

Add a `FoliageBender3D` to the player, an animal or each wheel of a car and the plants round it bend away and are pressed down as it goes through. `radius` is how far it reaches. The 16 nearest the camera count.

Your own shaders can follow the weather too. `Weather3D` sets these shader globals, which you declare in a shader to use:

```glsl
global uniform float weather_wetness; // 0 dry to 1 soaked
global uniform float weather_rain;    // 0 to 1, how hard it's raining

void fragment() {
	// Wet ground: darker, and shinier.
	ALBEDO *= mix(1.0, 0.6, weather_wetness);
	ROUGHNESS = mix(ROUGHNESS, 0.1, weather_wetness);
}
```

For snow in a shader of your own, `weather_snow()` is there in every lit standard material; in a shader material, declare the globals it reads and copy the function from a `StandardMaterial3D` converted to a `ShaderMaterial`. It takes a world position and normal and gives how much snow lies there, from 0 to 1, with the weather's map of what's overhead (`weather_shelter`) keeping it off sheltered ground.

There are also `weather_cloud_cover`, `weather_overcast`, `weather_snow`, `weather_snow_cover`, `weather_lightning`, `weather_cloud_drift` and `weather_wind` (metres a second, x east and y south). From `TimeOfDay` there's `time_of_day_sun_direction` (towards the sun even when it's below the horizon), `time_of_day_moon_direction`, `time_of_day_star_rotation` and `time_of_day_night` (0 by day, 1 at night: for windows that glow after dark).

