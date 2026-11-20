# An island of hills generated from noise, grassy where it's flat and rocky on slopes (the
# material's auto shader), with the built-in ocean round it. The camera circles it; hold the left
# mouse button and drag to look round, and scroll to come closer.
extends Node3D

const SIZE := 512

var terrain: Terrain3D
var camera: Camera3D
var yaw := 0.0
var pitch := -25.0
var distance := 420.0


func _ready() -> void:
	var environment := WorldEnvironment.new()
	environment.environment = Environment.new()
	environment.environment.background_mode = Environment.BG_SKY
	environment.environment.sky = Sky.new()
	environment.environment.sky.sky_material = ProceduralSkyMaterial.new()
	add_child(environment)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-40, 40, 0)
	sun.shadow_enabled = true
	add_child(sun)

	camera = Camera3D.new()
	camera.far = 8000.0
	add_child(camera)

	terrain = Terrain3D.new()
	terrain.material = Terrain3DMaterial.new()
	terrain.material.auto_shader_enabled = true
	terrain.material.world_background = Terrain3DMaterial.NONE # Only sea round the island.
	terrain.ocean_enabled = true
	terrain.assets = Terrain3DAssets.new()
	terrain.assets.set_texture_list([_texture("Rock", Color(0.45, 0.42, 0.38), 0), _texture("Grass", Color(0.32, 0.45, 0.2), 1)])
	add_child(terrain)
	terrain.ocean_material.clarity = 2.0 # Murky enough to hide the sea floor.
	terrain.data.import_images([_island(), null, null], Vector3(-SIZE / 2.0, 0, -SIZE / 2.0), 0.0, 1.0)


# A plain texture with some speckle, standing in for real ones. Its alpha is its height.
func _texture(asset_name: String, colour: Color, id: int) -> Terrain3DTextureAsset:
	var noise := FastNoiseLite.new()
	noise.frequency = 0.08
	var image := Image.create_empty(256, 256, false, Image.FORMAT_RGBA8)
	for y in 256:
		for x in 256:
			var speckle := noise.get_noise_2d(x, y) * 0.15
			image.set_pixel(x, y, Color(colour.r + speckle, colour.g + speckle, colour.b + speckle, 0.5 + speckle))
	image.generate_mipmaps()
	var texture := Terrain3DTextureAsset.new()
	texture.name = asset_name
	texture.id = id
	texture.albedo_texture = ImageTexture.create_from_image(image)
	texture.uv_scale = 0.2
	return texture


# Hills that fall away into the sea towards the edges.
func _island() -> Image:
	var noise := FastNoiseLite.new()
	noise.frequency = 0.004
	noise.fractal_octaves = 5
	var image := Image.create_empty(SIZE, SIZE, false, Image.FORMAT_RF)
	for y in SIZE:
		for x in SIZE:
			var from_middle := Vector2(x, y).distance_to(Vector2(SIZE, SIZE) / 2.0) / (SIZE / 2.0)
			var falloff := 1.0 - smoothstep(0.4, 1.0, from_middle)
			image.set_pixel(x, y, Color((noise.get_noise_2d(x, y) * 0.5 + 0.5) * 40.0 * falloff - 40.0 * (1.0 - falloff) - 2.0, 0, 0))
	return image


func _process(_delta: float) -> void:
	var direction := Basis.from_euler(Vector3(deg_to_rad(pitch), deg_to_rad(yaw), 0)) * Vector3.BACK
	camera.position = direction * distance + Vector3.UP * 20.0
	camera.look_at(Vector3.ZERO)
	if not Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT):
		yaw += _delta * 3.0


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and Input.is_mouse_button_pressed(MOUSE_BUTTON_LEFT):
		yaw -= event.relative.x * 0.3
		pitch = clamp(pitch - event.relative.y * 0.3, -85.0, -5.0)
	elif event is InputEventMouseButton and event.pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			distance = max(distance * 0.9, 40.0)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			distance = min(distance * 1.1, 1500.0)
