# Graphics settings

`RecommendedGraphics` picks graphics settings for the computer the game runs on, in four tiers: `TIER_ULTRA`, `TIER_HIGH`, `TIER_MEDIUM` and `TIER_LOW`.

The player's tier is the option `display/graphics_tier` ([Options](/modules/options/)): `"ultra"`, `"high"`, `"medium"` or `"low"`, starting as the tier recommended for their computer. Purr applies it as the game starts and whenever it changes, so a graphics menu only needs to set it. When the game runs in a headset, it applies the tier's headset settings. Add the game's own settings for each tier as it changes:

```gdscript
func _ready() -> void:
	_apply_tier(Options.get_value("display/graphics_tier"))
	Options.changed.connect(func(key, value):
		if key == "display/graphics_tier":
			_apply_tier(value))


func _apply_tier(tier: String) -> void:
	crowd_density = { "ultra": 1.0, "high": 1.0, "medium": 0.6, "low": 0.3 }[tier]
```

To set the engine's settings yourself instead, turn off `rendering/graphics_tier/apply` in Project Settings. The option is still there for your menu, and `get_settings()` and `apply()` do the work:

```gdscript
RecommendedGraphics.apply(get_viewport(), RecommendedGraphics.get_settings(RecommendedGraphics.TIER_HIGH))
```

`get_tier()` is a first guess from the graphics card. It reads the card's generation and class from its name (an RTX 4070 is generation 40, class 70; an M4 Max is generation 4, Max), so cards that come out later get a sensible tier without an update. Laptop graphics sit a step below the desktop card of the same name, and integrated graphics (including phones, tablets and Apple TV) start low. Cards from families it doesn't know go by whether they're discrete.

`get_settings(tier)` gives the engine settings for a tier as a dictionary of `Viewport` properties (upscaling, anti-aliasing, texture filtering, model detail) plus the sun's shadow size, and `apply()` sets them. They take the screen into account: each tier below ultra draws at most so many pixels, so it upscales more on a 4K, 5K or ultrawide screen than on a 1080p one. Upscaling uses MetalFX on Apple devices and FSR elsewhere, temporal where the tier smooths with TAA. With the Mobile renderer, which has no TAA, it smooths with FXAA instead.

In a headset, use `get_settings(tier, true)`. It never uses TAA, which smears as the head turns, and smooths with MSAA instead. The lower tiers limit the pixels drawn for each eye, so a high-resolution headset upscales more.

## Measuring

A first guess can't know how heavy the game is. `benchmark()` measures each tier in the game itself, best first, and keeps the first one that holds a frame rate:

```gdscript
const TIERS := ["ultra", "high", "medium", "low"]


func _on_use_recommended_pressed() -> void:
	await RecommendedGraphics.benchmark(func(tier): Options.set_value("display/graphics_tier", TIERS[tier]), 60.0)
```

Each tier is applied with your function, left to settle for a moment, then every frame is timed for a few seconds. A tier holds the frame rate when nine frames in ten are on time, so a tier that averages 60 but stutters doesn't pass. The whole run takes up to about 20 seconds. Run it somewhere representative of the game, such as behind the settings menu over a busy scene. It runs while the game is paused. The tier chosen stays applied.

Vertical sync and `Engine.max_fps` are switched off while it measures and put back after, so a 60 Hz display doesn't hide how fast a tier can go. In a headset, pass the headset's refresh rate: the headset paces the frames, and the benchmark measures whether each tier keeps up.

## Dynamic resolution

`DynamicResolution` changes the 3D resolution as the game runs to hold the frame rate. A heavy scene draws at a lower resolution and a light one at full size, and the upscaler makes up the difference. It's off until the game turns it on, so offer it as a setting:

```gdscript
func _apply_tier(tier: String) -> void:
	DynamicResolution.max_scale = get_viewport().scaling_3d_scale
	DynamicResolution.enabled = Options.get_value("game/dynamic_resolution")
```

It holds 60 frames a second on a screen (less on a slower display, or with `Engine.max_fps` lower), the headset's refresh rate in VR, or `target_fps` if you set one. On a 120 Hz display it stays at 60, as most players would rather have a sharp picture than a soft one at 120; set `target_fps` to aim higher (from a "prefer frame rate" option, say), or turn vertical sync off, which aims for the display's full rate. It draws between `min_scale` (half size by default; below that, upscaling looks soft) and `max_scale`. It lowers the resolution as soon as frames would be late and raises it a step at a time once there's room, so a moment's lull doesn't cause a stutter.

It measures how long the GPU takes to draw the 3D world, so it works with vertical sync on. When frames come late for reasons it can't see, such as other viewports drawing on the GPU, it leaves itself more room. If the game is held back by the CPU instead, lowering the resolution wouldn't help, so it doesn't.

It changes the main viewport unless you set `viewport`, for a game that draws its 3D world in a `SubViewport` (a headset's view drawn in one with `use_xr`, say).

It works with any upscaler. If the viewport has none, it uses the platform's while it's on. Temporal upscalers (FSR 2 and MetalFX's, with TAA) hide the changes best; in VR, where TAA smears as the head turns, it uses a spatial one. Turning it off puts the viewport's own upscaler and scale back. It pauses while `benchmark()` runs, so each tier is measured as it is.

## Long frames

`FrameBudget` says what made a frame long. Time your game's heavy work by name:

```gdscript
var started := Time.get_ticks_usec()
rebuild_paths()
FrameBudget.measure("paths", started)
```

A frame taking 40 ms or more (`frame_budget/long_frame_ms`) then prints what took its time, once a second at most:

```
Long frame: 63 ms (city 30.4 ms, paths 12.1 ms; the rest drawing and the engine's own).
```

Reports are on in debug builds, except headless ones, whose frames draw nothing. In release builds, turn on `frame_budget/report_in_release_builds`, or set `FrameBudget.reporting` from a playtest option. The `long_frame` signal gives each long frame with its measured work, to show in your own overlay or send with a bug report.

## Spreading heavy work

Work that can wait, such as building collision for what just loaded, runs a slice at a time within a budget each frame:

```gdscript
func _make_solid() -> bool:
	add_collision(waiting.pop_back())
	return not waiting.is_empty() # true: more to do next time

func _ready() -> void:
	FrameBudget.spread("collision", _make_solid, 2.0) # 2 ms a frame
```

The step is called again and again until it returns `false` or its object is freed. While nobody can see a slow frame (a fade to black, a loading screen), set `FrameBudget.screen_hidden` and spread work gets six times its budget.
