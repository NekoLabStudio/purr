# Terrain

`Terrain3D` is a landscape you sculpt and paint in the editor: hills, valleys, textured ground, holes for caves, grass and rocks scattered over it, and an ocean round it. It draws kilometres of ground quickly, finer near the camera and coarser further away, and has collision.

Add a `Terrain3D` node to a 3D scene and select it. Its tools appear in the 3D view's toolbar.

## Shaping the ground

Terrain is made of regions: squares of `region_size` metres (256 by default). There's ground only where there are regions, so start with **Add Region** and drag over where you want ground. **Remove Region** takes regions away again.

Then shape it:

- **Raise** and **Lower** build the ground up or dig it down.
- **Smooth** evens out bumps.
- **Flatten** brings the ground to the height set beside the tools.
- **Hole** cuts holes in the ground, for caves and tunnels. Hold Ctrl (Cmd on a Mac) to fill them in again.

**Size** and **Strength** set the brush. Every stroke can be undone.

To start from a heightmap made elsewhere (in Gaea, World Machine, or from real elevation data), choose **Terrain > Import Heightmap...** in the toolbar. It reads PNG (16-bit ones at their full precision), EXR and other images, and raw 16-bit `.r16` files. Say what heights the image's darkest and brightest points stand for, and where its top-left corner goes (by default, the image is centred on the origin). One pixel becomes one vertex, `vertex_spacing` apart. Importing can be undone too.

When you save the scene, the regions are saved in a folder beside it, named after it: `forest_terrain` for `forest.tscn`. To keep them somewhere else, set `data_directory` first.

## Textures

**Texture** paints a texture on the ground, and **Blend** mixes one into what's already there. Choose the texture beside the tools, and add one with **+**: it opens in the inspector, where you choose its images.

A texture has two images:

- `albedo_texture`: its colour, with its height in the alpha channel (used to blend textures realistically).
- `normal_texture`: its normal map, with its roughness in the alpha channel.

Most texture packs come with the height and roughness as images of their own. Give those to `height_texture` and `roughness_texture` and the editor packs them for you, into files beside the originals (`grass_terrain.png` beside `grass.png`), imported compressed with mipmaps. They're packed again when you change one of the images.

All of a terrain's textures must have the same size and import settings (with mipmaps), since they're drawn together.

Turn on the material's `auto_shader_enabled` and the ground textures itself wherever you haven't painted: the second texture on flat ground and the first on slopes, so a new landscape looks natural before you paint anything. Painting a texture takes the ground out of the auto shader's hands; **Auto Texture** gives it back (hold Ctrl or Cmd to take it away).

**Slope** limits **Texture**, **Blend** and **Place** to ground between two steepnesses: rock only on cliffs, grass only where it's fairly flat.

**Color** tints the ground, over its textures.

## Grass, rocks and trees

**Place** scatters the chosen mesh over the ground where you drag, and **Remove** takes it away. A terrain starts with a mesh of grass cards; add your own with **+** and choose a scene in its `scene_file`. Each mesh has a `density`, distances at which it switches to simpler versions (`lod0_range` and so on) and its shadows.

Placed meshes are drawn in batches, so thousands of them cost little. They're for scenery: anything the player interacts with should be a node of its own.

## Navigation

Put the terrain inside a `NavigationRegion3D` and the region's bake includes the ground, as it does any mesh. To keep characters to paths, paint where they can walk with **Navigation** (hold Ctrl or Cmd to unmark): once any of the terrain is marked, a bake covers only the marked parts.

## In the game

The terrain follows the viewport's camera, drawing finer ground near it. To follow something else, such as the player in a split screen, set `clipmap_target`.

`data` answers questions about the ground:

```gdscript
var ground := terrain.data.get_height(player.global_position)
if not is_nan(ground):
	player.global_position.y = ground
```

`get_height()` is `NAN` where there's no ground: outside the regions, or in a hole. `get_normal()` gives the slope.

Collision is made around the camera as it moves (`collision_mode`), so bodies near the camera collide with the ground without the cost of collision for the whole landscape. Set `collision_target` to make it round something else.

## Large worlds

Terrain works with a [floating origin](/modules/world/#floating-origin): when `WorldOrigin` shifts the world, the ground, its collision, its meshes and the ocean stay where they are on the map. The terrain's `data` (and its `instancer`) works in map positions, which are the same as global ones until the world first shifts, so convert positions when you use one:

```gdscript
var ground := terrain.data.get_height(WorldOrigin.to_map(player.global_position))
```

Node methods, such as `get_intersection()`, take and return global positions as usual.

A landscape too big to keep in memory streams its regions. Set `region_load_radius`, and in the game only the regions within that distance of the camera are loaded: those ahead are read in the background as the camera nears them, and those well behind are dropped. Make the radius reach the horizon, since the terrain is what the player sees furthest away. Before putting the player somewhere new, call `load_regions_around()` so the ground is there when they arrive:

```gdscript
terrain.load_regions_around(destination)
player.global_position = destination
```

The editor always loads every region, so all of it can be edited.

## Ocean

Turn on `ocean_enabled` for water round and between the land, which also stretches to the horizon. It comes with an animated ocean material whose sea level, colour, waves and foam are its shader parameters.

## Credits

Terrain is based on [Terrain3D](https://github.com/TokisanGames/Terrain3D), by Cory Petkovsek, Roope Palmroos and its contributors (MIT). Its classes, data files and settings keep Terrain3D's names, so a project made with the Terrain3D plugin keeps its terrain in Purr: remove the plugin's folder, and replace anything a scene used from that folder (its example ocean material is built in here: clear `ocean_material` to use it).
