# Large worlds

Purr's world module is for games whose worlds stretch for kilometres: a city, an island, a country to drive across.

## Floating origin

Far from (0, 0, 0), single-precision positions step in millimetres, and what's near the camera starts to shimmer and jitter. `WorldOrigin` moves the whole world back under the player in whole steps (a kilometre by default), so the player always stays near the origin.

Tell it where the player is every frame:

```gdscript
func _physics_process(_delta: float) -> void:
	WorldOrigin.follow(player.global_position, player.velocity.length() < 1.0)
```

The second argument says whether it's a calm moment, when a shift's moment of work won't be noticed: the player standing still, or the screen dark. Past `world_origin/calm_distance` (1.5 km) from the origin it shifts at the next calm moment, and past `world_origin/max_distance` (4 km) it shifts whatever's happening.

Everything under the current scene moves with it. To move another node instead, set `WorldOrigin.world`.

### Map positions

A shift changes global positions, but not where things are on the map. Map positions are what to save, send to other players or keep in your data:

```gdscript
save["position"] = WorldOrigin.to_map(player.global_position)
player.global_position = WorldOrigin.to_global(save["position"])
```

`transform_to_map()` and `transform_to_global()` do the same for whole transforms. A system that keeps global positions of its own (in an array, say) adds each shift as it happens:

```gdscript
WorldOrigin.shifted.connect(func(by: Vector3) -> void:
	for i in targets.size():
		targets[i] += by)
```

Shaders find a vertex's map position with the `map_offset` global shader parameter, for textures that stay put on the ground as the world shifts:

```glsl
vec3 map_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz + map_offset;
```

Shifts are whole metres, so `WorldOrigin.offset` is exact even thousands of kilometres out.

### What a shift costs

A shift moves everything at once on the renderer and in physics, rather than node by node: on a city of about 32,000 nodes it costs about 30 ms over two frames. Shift at a calm moment and nobody sees it. Physics interpolation is reset, so nothing is drawn sliding a kilometre, and particles simulating in world space start afresh, as Godot can't move them: give them a preprocess time and nobody notices.

Nodes that don't hang from the world (or are `top_level`) stay where they are. Each one is put back on the renderer and in physics after a shift, so keep them to the few that need it. Objects you made yourself on the `RenderingServer` or `PhysicsServer3D` in the world's scenario or space move with it.

## Streaming

A world too big to load at once is split into cells, each a scene of its own. `WorldStreamer` loads the cells round the player and drops those they leave behind. Add one under your world, at its origin, and tell it about the cells:

```gdscript
func _ready() -> void:
	for x in 40:
		for z in 40:
			var name := "%d_%d" % [x, z]
			$WorldStreamer.add_cell(name, Rect2(x * 250, z * 250, 250, 250), "res://world/%s.tscn" % name)
```

A cell's area is on the map, in metres along x and z. Cells needn't be the same size or on one grid. Make cell scenes in map coordinates: each is placed at the streamer's origin, so a building 3 km east is at x = 3000 in its cell's scene.

Cells within `load_radius` of the player (or the camera, unless you set `observer`) load in the background, and join the world a few nodes a frame within `budget_ms`, so their collision, lights and meshes arrive in slices and moving never waits. Past `unload_radius` they leave the same way: hidden at once, freed over the next frames. Keep `unload_radius` above `load_radius`, so a cell at the edge isn't loaded and dropped over and over.

### Far scenes

Beyond the detail, a cell can show something lighter: a few blocks for its buildings, without collision. Give it a far scene, and set `far_radius`:

```gdscript
$WorldStreamer.add_cell(name, area, "res://world/%s.tscn" % name, "res://world/%s_far.tscn" % name)
```

A far scene shows wherever its cell's detail isn't loaded, out to `far_radius`.

### Your own work on each cell

`prepare` is called for each cell before it joins the world, on a worker thread, for heavy work such as building collision from its meshes or placing scattered props:

```gdscript
$WorldStreamer.prepare = func(name: String, node: Node, far: bool) -> void:
	if not far:
		add_props(node, name)
```

The cell isn't in the tree yet, so keep to the cell itself there. `cell_loaded` and `cell_unloading` tell you as each one comes and goes, on the main thread.

Cells are instantiated on worker threads, several at once, so resources shared between cell scenes must be safe to read from several threads. Imported meshes are. A `PrimitiveMesh` (a `BoxMesh`, say) isn't, unless you make it local to the scene.

### Props with collision

Scattered props (bollards, benches, rocks) are cheapest drawn as a `MultiMeshInstance3D`, but Godot gives its instances no collision. `MultiMeshBody3D` does, one shape per instance:

```gdscript
var body := MultiMeshBody3D.new()
body.multimesh = $Bollards.multimesh
body.shape = preload("res://props/bollard_shape.tres")
$Bollards.add_child(body)
```

Its shapes are made a few dozen at a time over frames, the nearest the camera first, within one budget for all of them (`physics/3d/multimesh_collision_budget_ms`, a millisecond a frame by default): a cell full of props never holds up a frame, and what's ahead of the player is solid before the skyline. A raycast that hits one reports the `MultiMeshBody3D`, and `get_instance(hit.rid, hit.shape)` says which prop it was. Give collision only to the kinds that need it: a car can drive through a flower bed.

### Putting the player somewhere

When the player arrives somewhere new, load the ground round them at once, behind a loading screen or a fade:

```gdscript
$WorldStreamer.load_around(WorldOrigin.to_map(spawn.origin), 200.0)
while $WorldStreamer.is_busy():
	await get_tree().process_frame
```

`is_busy()` stays `true` while cells near the player are still wanted, loading or joining. Headless, everything loads at once on the main thread, so tests run the same every time.

## Lights at night

A city at night has thousands of lamps and lit windows. Each can glow, but only a few real lights can light the street before the frame rate suffers. `LightPool3D` keeps a few lights on the points that matter most round the player, and moves them as the player goes.

Add one to your world with a light as its child: the pool's lights are copies of it. For street lamps, a `SpotLight3D` pointing down, without shadows. Then give it the points:

```gdscript
for lamp in street_lamps:
	$LampPool.add_point(lamp.position, lamp.strength)
```

Points are in the pool's own frame: map coordinates, with the pool at the world's origin. They rank by distance over the square root of their strength, so a bright lamp further away can beat a dim one nearby. Lights fade in and out as the ranking changes rather than popping, so nobody sees them move. Fade the whole pool in at dusk with `brightness`.

Points can change after they're added: `set_point_strength()` (0 for a shop that's shut) and `set_point_color()` (a video screen's light following its pictures).

Use a pool for each kind of light, each with its own template and `count`: lamps, shop windows, video screens. `height_weight` above 1 makes points far above or below the player count as further away, so the lamps on a viaduct overhead don't take every light from the street beneath. Lights without shadows shine through floors, so set `reach_above` to leave out points too far above the player altogether.

## Reflections

Wet streets, glass and car paint should reflect the place round them, but a large world can't have reflection probes everywhere, and screen-space reflections only show what's on screen. `ReflectionProbePool` places a few probes at the sites nearest the player, such as each junction of a city's streets, and moves them as the player goes.

Add one with a `ReflectionProbe` as its child, set up as every probe should be: the pool's probes are copies of it. Then give it the sites, each with its box's middle, size and where it captures from:

```gdscript
for junction in junctions:
	var middle := Transform3D(junction.basis, junction.position + Vector3.UP * 9.0)
	$Reflections.add_site(middle, Vector3(junction.width, 18.0, junction.width), Vector3(0, -7.5, 0))
```

Capturing a probe draws the scene round it, so new ones are placed one a frame. Leave what moves out of the template's `cull_mask`: captured once, a passing car would stay in every puddle. When the light changes enough to show (dusk, rain), call `recapture()` and they're captured again, one a frame. In a world with levels, `height_reach` leaves out sites too far above or below the player: on a viaduct, the junctions of the street beneath aren't its own.

Sites needn't all be added at the start. A game whose sites come from data round the player (a road network's junctions, the water in the tiles loaded) can add them as the player nears them and remove them as they're left behind: the pool still picks the nearest and places them one a frame.

## Many moving copies

A crowd, an army, a shoal or a storm of bullets is thousands of copies of one mesh that move every frame. As nodes they'd be slow, and as one `MultiMeshInstance3D` every copy is drawn whenever any is in view, with its shadows, at the detail the nearest one needs. `InstanceGroup3D` draws them in chunks of the world (`chunk_size`, 64 m by default), so chunks behind the camera or past `visibility_range_end` cost nothing, and an imported mesh's simpler levels of detail are drawn for the chunks far away.

Give it every copy's transform once a frame, as 12 floats each in the order a `MultiMesh` buffer has them (the basis's rows, each followed by one coordinate of the origin):

```gdscript
var floats := PackedFloat32Array()


func _process(delta: float) -> void:
	floats.resize(fish.size() * 12)
	for i in fish.size():
		var t: Transform3D = fish[i].transform
		var o := i * 12
		floats[o] = t.basis.x.x; floats[o + 1] = t.basis.y.x; floats[o + 2] = t.basis.z.x; floats[o + 3] = t.origin.x
		floats[o + 4] = t.basis.x.y; floats[o + 5] = t.basis.y.y; floats[o + 6] = t.basis.z.y; floats[o + 7] = t.origin.y
		floats[o + 8] = t.basis.x.z; floats[o + 9] = t.basis.y.z; floats[o + 10] = t.basis.z.z; floats[o + 11] = t.origin.z
	$Shoal.set_transforms(floats)
```

The transforms are the node's own space, and the count is however many the array holds. Turn on `use_colors` or `use_custom_data` to give each copy a colour or four numbers for its shader, with `set_colors()` and `set_custom_data()`. Whatever changes in a frame is drawn once, at the end of it.

Over a 1 km field of 20,000 moving spheres with the sun's shadows, the GPU took 20 ms a frame with one `MultiMeshInstance3D` and 2.5 ms with an `InstanceGroup3D`. Larger chunks draw fewer, bigger batches; smaller ones cull more closely. 64 m suits most scenes, and `get_chunk_count()` says how many there are. It follows `WorldOrigin` like any node, and its `layers` work as on any visual node: leave a crowd out of a reflection probe's `cull_mask` so it isn't captured into every puddle.
