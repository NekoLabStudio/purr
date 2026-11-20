# Scattering

`Scatter3D` fills a box with copies of a mesh: rocks over a hillside, grass in a field, debris in a yard. Add one to a 3D scene, give it a `mesh`, and drag the box's handles over the ground. Each copy is dropped from the top of the box onto the first surface below it, so they sit on whatever is there: floors, rocks, a [terrain](/modules/terrain/).

The copies are drawn as one `MultiMesh`, so thousands cost little. In the editor they're placed again as you change the settings or move the box, and they're saved with the scene, so the game doesn't place them again when it loads.

## Settings

- `density`: how many copies to try for each square metre of the box's floor.
- `seed`: which random arrangement. Change it for another one with the same settings.
- `scale_min` and `scale_max`: each copy is scaled somewhere in between.
- `random_rotation`: turns each copy to a random direction.
- `align_to_surface`: how far copies lean with the slope they land on, from upright (`0`) to lying along it (`1`).
- `max_slope`: copies don't land on anything steeper, so grass stays off cliffs.
- `surface_mask`: the physics layers copies land on. A terrain is always a surface.

Copies only land on physics bodies, so a floor or rock needs collision for things to land on it.

## Solid copies

Set `collision_shape` and each copy gets that shape, placed and sized with it, on `collision_layer`. The shapes are made over a few frames, nearest the camera first, through a [`MultiMeshBody3D`](/modules/world/#props-with-collision), and a raycast that hits one reports that body. Give collision only to what needs it: a player walks round boulders but through grass.

## From code

A `Scatter3D` made in code places its copies when it's ready. To place them again after changing it, or what's below it, call `scatter()`:

```gdscript
$Rocks.seed = randi()
$Rocks.scatter()
```

## Far away

Far off, a tree's thousands of leaves cover a few pixels but cost as much as up close. Bake the tree into an `Impostor`, a picture of it from many directions, and give it to the scatter: past `impostor_distance` (60 m by default), each copy is drawn as a single quad turned to the camera, showing the tree as seen from about that direction, fading over from the real ones.

```gdscript
@tool
extends EditorScript

func _run() -> void:
	var impostor := Impostor.new()
	var result: Dictionary = await impostor.bake(get_scene().get_node("Oak"))
	if result.ok:
		ResourceSaver.save(impostor, "res://trees/oak_impostor.res")
```

The distance is measured to the whole scatter, so give each copse or hillside its own scatter rather than one for the whole map. Impostors are baked by rendering, so in the editor or as the game loads, not headless.

