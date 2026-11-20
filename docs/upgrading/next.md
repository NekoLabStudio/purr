# Unreleased

<!-- unreleased-intro-start (support/prepare-release.sh drops this block at release) -->
The notes for the next release: everything below is in `main` and ships
together when the version is tagged.
<!-- unreleased-intro-end -->

## What's new

- `StaticScenery3D`: put the parts of a scene that never move under it, and the game draws them as a few merged meshes, one for each material in each chunk of the world, with simpler versions for distance and occluders made from walls and buildings. The originals stay in the tree, hidden, so their collision and scripts work as before. See [Large worlds](/modules/world/#static-scenery).
- Terrain hides what's behind its hills: each region has a coarse occluder just under its ground (`occluders_enabled`). See [Terrain](/modules/terrain/#in-the-game).
- The sun's far shadow splits are drawn every few frames instead of every frame, and drawn again at once when the camera, the sun or the settings change. In a large scene this saves much of the shadows' cost (`rendering/lights_and_shadows/directional_shadow/cache_far_splits`).
- VR on Windows and Linux draws the edges of each eye's view more coarsely (foveated rendering), where the headset's runtime or the GPU can. See [VR](/modules/vr/#foveated-rendering).
- Screen effects: `MotionBlurEffect`, `LightShaftsEffect`, `LensFlareEffect`, `VignetteEffect`, `FilmGrainEffect`, `ChromaticAberrationEffect` and `LocalExposureEffect`, added to a `Compositor` like any compositor effect. Light shafts and the lens flare follow the sun. See [Graphics settings](/modules/graphics/#screen-effects).
- The graphics tiers turn on screen-space contact shadows in Ultra and High, for small things meeting the ground. See [Graphics settings](/modules/graphics/).
- `Weather3D`: clouds and rain that change gradually, wet ground that dries slowly, rain round the camera, and shader globals for your own wet materials. `CloudySkyMaterial`: a sky that follows `TimeOfDay`'s sun through dusk and night, with clouds that follow the weather. See [Time of day and weather](/modules/time-of-day/#weather).
- Occlusion culling works in VR: each eye is culled from where it is, so nothing one eye sees past a wall's edge pops in and out. If your game turned occlusion culling off in the headset for this, turn it back on.
- Occluders hidden before they're freed no longer stay in memory: a world that streams its occluders in and out grew with every one.
- `FoliageMaterial`: grass, leaves and plants that sway in `Weather3D`'s wind, with gusts rolling across the world. See [Time of day and weather](/modules/time-of-day/#wind).
- `PhotoMode`: open it from your pause menu and players can stop the game, fly a camera round, set focus, blur, exposure, a vignette and grain, and save photos. See [Cameras](/modules/cameras/#photo-mode).
- `BakedAnimations` and `BakedAnimationMaterial`: bake a skinned character's animations into textures and draw thousands of it with `InstanceGroup3D`, each in its own animation, as cheaply as still meshes. See [Large worlds](/modules/world/#animated-crowds).
- `Impostor`: bake a tree (or anything) from many directions into one texture, and draw it far away as a single quad. `Scatter3D` switches to it past `impostor_distance`. See [Scattering](/modules/scattering/#far-away).
- `rendering/textures/quality/skip_mipmaps`: load 3D textures without their largest levels of detail, for less video memory. The Low graphics tier uses it. See [Graphics settings](/modules/graphics/).
- `AtmosphereSkyMaterial`: the sky worked out from the air (Hillaire's technique), with the sun's disc, dusk's reds and volumetric clouds that follow the weather. `AerialPerspectiveEffect` lays the same air over the scene. See [Time of day and weather](/modules/time-of-day/#the-sky-as-the-air-makes-it).
- `Weather3D` has snow, fog and storms: `snow` falls and settles on terrain and plants (`settling_time`, `melting_time`), `fog` thickens the world's fog, and `lightning` flashes in a storm, with `lightning_flashed` for the thunder. Rain splashes where it lands. See [Time of day and weather](/modules/time-of-day/#weather).
- `WetSurfacesEffect`: the world gets wet with the rain, whatever its materials, with puddles that reflect and ripple, and dry ground under roofs. See [Time of day and weather](/modules/time-of-day/#wet-surfaces).
- The skies have stars and the Milky Way, turning through the night, and a moon with phases (`TimeOfDay.moon_phase`). See [Time of day and weather](/modules/time-of-day/#the-sky).
- `TimeOfDay` turns on everything in the `night_lights` group at dusk, one by one, and off at dawn. See [Time of day and weather](/modules/time-of-day/#lights-at-dusk).
- `FoliageBender3D`: plants bend away from players, animals and wheels going through them. See [Time of day and weather](/modules/time-of-day/#wind).
- `ColorGrade` and `ColorGradeVolume3D`: a look for a place, a time of day or a weather, blended in as the camera moves or the day turns. See [Graphics settings](/modules/graphics/#colour-grading).
- `VirtualCamera3D` can keep its target or what's in the middle of the view in focus (`focus_mode`), and fade out what comes between it and the player (`fade_obstacles`). See [Cameras](/modules/cameras/#focus).
- `Water3D` and `WaterMaterial`: lakes, rivers and an endless sea, with waves that follow the wind, depth, foam and rain, and `get_height_at()` for things that float. See [Water](/modules/water/).
- New projects start with Purr's logo as their icon (`icon.svg`) instead of Godot's. Replace it with your game's own.
- `GroundMarks3D`: tyre marks, footprints and ski tracks that fade. See [Ground marks](/modules/ground-marks/).
- `InteriorMaterial`: windows with rooms behind them and no geometry inside, lit at night. See [Large worlds](/modules/world/#windows).
- An MCP server for AI assistants, built into the editor: `purr --path <project> --mcp`. Assistants such as Claude Code, Codex, Antigravity and OpenCode can search Purr's docs and the class reference, read the project, check it, run its tests and scenes, take screenshots, and reach the open editor and the game it's running. See [AI assistants](/start/ai-assistants/).
- The GPU's time for each frame is a custom performance monitor, `graphics/gpu_ms`. See [Graphics settings](/modules/graphics/#measuring).

## Changes to check

- Occlusion culling is now on by default (`rendering/occlusion_culling/use_occlusion_culling`). It costs nothing in scenes without occluders, and projects with `OccluderInstance3D` nodes now use them without turning it on. If your project relied on it being off, turn it off in Project Settings.
- New export presets bake shaders (`shader_baker/enabled`), so players don't wait on shaders compiling, at the cost of longer exports. Presets you already have keep their setting: turn it on in each preset to get the same.
- Moving objects' shadows in the sun's far splits follow a frame or a few behind. If that shows in your game, turn off `rendering/lights_and_shadows/directional_shadow/cache_far_splits`.
- On Windows and Linux, VR starts with foveated rendering (`xr/openxr/foveation_level` Medium, `rendering/vrs/mode` XR) unless your project sets those itself. Set them to Off and Disabled to keep full detail.
- The graphics tier now also sets screen-space contact shadows (`rendering/lights_and_shadows/contact_shadow/enabled`): on in Ultra and High, off in Medium, Low and a headset. A project that turned them on itself loses them in the lower tiers; turn off `rendering/graphics_tier/apply` to set them yourself.
- The Low graphics tier now loads 3D textures at half their size (`rendering/textures/quality/skip_mipmaps`), for less video memory.
- New projects use physics interpolation (`physics/common/physics_interpolation`), so what moves in physics ticks moves smoothly on any screen. Projects you already have keep their setting. Turning it on in one costs little (4,000 moving bodies take about a quarter of a millisecond a frame), but anything teleported needs `reset_physics_interpolation()` after it moves.
- Rain no longer needs `GPUParticlesCollision3D` nodes under roofs: `Weather3D` keeps rain and snow off whatever's overhead by itself. You can remove the ones you added for it.
- `TimeOfDay`'s moon is no longer always opposite the sun: it goes through its phases, starting full (`moon_phase`). A new moon gives no light, so nights get darker over the month. Set `moon_phase` (from `hour_changed` at midnight, say) for the moon you want.
- Terrain's ocean is now an endless `Water3D` with a `WaterMaterial`, the same water as everywhere else: waves that follow the wind, foam, rain and floating. Its sea level is `ocean_level` (it was the old material's `sea_level`), and the ocean's mesh settings (`ocean_mesh_lods`, `ocean_mesh_size`, `ocean_vertex_spacing`, `ocean_tessellation_level`, `ocean_cull_margin`, `ocean_cast_shadows`, `ocean_gi_mode`, `ocean_render_layers`) are gone. A terrain with an ocean material of its own keeps it on the new sea, but the old ocean shader's own parameters no longer do anything: clear `ocean_material` for the new water.
- With vertical sync on, a game that can't keep up with the screen is now capped to an even share of its refresh rate (on a 60 Hz screen, 30 rather than an uneven 40 or so), and goes back up when it can. In a headset it caps only to 72 or more. A game that sets `Engine.max_fps` itself is left alone; turn it off with `display/window/vsync/even_frame_pacing`. See [Graphics settings](/modules/graphics/#frame-pacing).
- Snow settles on every lit `StandardMaterial3D` (whatever faces up, not under roofs) as well as terrain and plants, so a game with snowy weather gets white roofs and streets without changing its materials. Each standard material's shader has a few more lines for it. Turn it off with `rendering/weather/snow_on_materials`.
- Crash reports no longer start in runs without a game (`--doctool` and the like), which could send what was waiting and leave a marker behind.
- The editor listens on a local port while a project is open, for the MCP server (`.godot/purr_mcp.json` says where). It answers only on your computer and only with the token in that file.
- A material that reads the screen (water, say) no longer fails in viewports a pixel or two across.
- On macOS, a run that fails to start (a script given with `-s` that doesn't parse, say) now exits with an error, as it does on Linux and Windows. Before, a test script that didn't load passed on a Mac.
- A game that quit with a console command, or work given to `FrameBudget.spread()`, still held by a script could crash on the way out (seen on Windows). Both now let go of them when the game's scene tree ends.
