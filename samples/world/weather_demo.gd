# A small town by the sea under a cloudy sky, to look at the weather, the sky, the water and the screen effects:
#   purr --path samples/world -s weather_demo.gd
# Keys: R rain on or off (the wind picks up with it), S snow, L a storm with lightning, F fog, C clear or cloudy,
# A the air's sky or the painted one, T an hour later, M the moon a week on, G colour grades on or off,
# E screen effects on or off, Space stop or start the camera.
extends SceneTree

var weather: Weather3D
var day: TimeOfDay
var compositor: Compositor
var camera: Camera3D
var turning := true
var label: Label
var sky: Sky
var walker: Node3D
var grades: Array[ColorGradeVolume3D] = []


func _initialize() -> void:
	var world := Node3D.new()
	root.add_child(world)

	var environment := WorldEnvironment.new()
	environment.environment = Environment.new()
	environment.environment.background_mode = Environment.BG_SKY
	environment.environment.sky = Sky.new()
	# The sky as the air makes it, with volumetric clouds; A switches to the painted CloudySkyMaterial.
	environment.environment.sky.sky_material = AtmosphereSkyMaterial.new()
	environment.environment.ambient_light_source = Environment.AMBIENT_SOURCE_SKY
	environment.environment.ssao_enabled = true
	compositor = Compositor.new()
	var blur := MotionBlurEffect.new()
	compositor.compositor_effects = [WetSurfacesEffect.new(), AerialPerspectiveEffect.new(), blur, LightShaftsEffect.new(), LensFlareEffect.new(), VignetteEffect.new(), FilmGrainEffect.new(), ChromaticAberrationEffect.new(), LocalExposureEffect.new()]
	environment.compositor = compositor
	world.add_child(environment)
	sky = environment.environment.sky

	day = TimeOfDay.new()
	day.time = 17.0
	day.day_length = 240.0
	for i in 2:
		var light := DirectionalLight3D.new()
		light.shadow_enabled = true
		day.add_child(light)
	world.add_child(day)

	weather = Weather3D.new()
	weather.cloud_cover = 0.45
	weather.change_time = 8.0
	world.add_child(weather)

	var ground := MeshInstance3D.new()
	ground.mesh = PlaneMesh.new()
	ground.mesh.size = Vector2(400, 400)
	world.add_child(ground)
	var floor_body := StaticBody3D.new()
	var floor_shape := CollisionShape3D.new()
	floor_shape.shape = WorldBoundaryShape3D.new()
	floor_body.add_child(floor_shape)
	world.add_child(floor_body)

	# The sea round the town's slab of ground.
	var sea := Water3D.new()
	sea.position = Vector3(0, -0.6, 0)
	world.add_child(sea)

	# A cool, paler look at night, and a greyer one in the rain.
	for when in [ColorGradeVolume3D.WHEN_NIGHT, ColorGradeVolume3D.WHEN_RAINING]:
		var grade := ColorGradeVolume3D.new()
		grade.global = true
		grade.when = when
		grade.grade = ColorGrade.new()
		if when == ColorGradeVolume3D.WHEN_NIGHT:
			grade.grade.tint = Color(0.85, 0.9, 1.0)
			grade.grade.saturation = 0.8
		else:
			grade.grade.saturation = 0.7
			grade.grade.contrast = 0.95
		world.add_child(grade)
		grades.append(grade)

	# A town of blocks and posts that never moves, merged into a few meshes.
	var town := StaticScenery3D.new()
	var rng := RandomNumberGenerator.new()
	rng.seed = 7
	var walls := StandardMaterial3D.new()
	walls.albedo_color = Color(0.75, 0.72, 0.68)
	var windows := InteriorMaterial.new()
	var roofs := StandardMaterial3D.new()
	roofs.albedo_color = Color(0.45, 0.25, 0.2)
	for x in range(-6, 7):
		for z in range(-6, 7):
			if abs(x) < 1 or abs(z) < 1:
				continue  # the streets the camera looks down
			var house := MeshInstance3D.new()
			house.mesh = BoxMesh.new()
			var height := rng.randf_range(4.0, 16.0)
			house.mesh.size = Vector3(8, height, 8)
			house.material_override = walls
			house.position = Vector3(x * 12.0, height / 2.0, z * 12.0)
			town.add_child(house)
			var roof := MeshInstance3D.new()
			roof.mesh = PrismMesh.new()
			roof.mesh.size = Vector3(8.5, 3, 8.5)
			roof.material_override = roofs
			roof.position = house.position + Vector3(0, height / 2.0 + 1.5, 0)
			town.add_child(roof)
			# Windows with rooms behind them on each side, three across and a floor every three metres.
			for side in 4:
				var front := MeshInstance3D.new()
				front.mesh = QuadMesh.new()
				front.mesh.size = Vector2(8, height)
				var rooms := windows.duplicate() as InteriorMaterial
				rooms.rooms = Vector2i(3, maxi(int(height / 3.0), 1))
				front.material_override = rooms
				var facing := Basis(Vector3.UP, side * PI / 2.0)
				front.transform = Transform3D(facing, house.position + facing * Vector3(0, 0, 4.02))
				town.add_child(front)
	world.add_child(town)

	# A shelter over the street: its ground stays dry in the rain and clear in the snow.
	var shelter := MeshInstance3D.new()
	shelter.mesh = BoxMesh.new()
	shelter.mesh.size = Vector3(6, 0.2, 5)
	shelter.position = Vector3(0, 3, 20)
	world.add_child(shelter)

	# Street lamps that come on at dusk.
	for i in range(-6, 7):
		var lamp := OmniLight3D.new()
		lamp.light_color = Color(1.0, 0.8, 0.55)
		lamp.light_energy = 2.0
		lamp.omni_range = 10.0
		lamp.position = Vector3(2.5, 4, i * 12.0 + 6.0)
		lamp.add_to_group(&"night_lights")
		world.add_child(lamp)

	# Grass along the street, swaying in the wind.
	var blades := MultiMeshInstance3D.new()
	blades.multimesh = MultiMesh.new()
	blades.multimesh.transform_format = MultiMesh.TRANSFORM_3D
	var blade := QuadMesh.new()
	blade.size = Vector2(0.08, 0.6)
	blade.center_offset = Vector3(0, 0.3, 0)
	var grass := FoliageMaterial.new()
	grass.albedo_color = Color(0.35, 0.55, 0.2)
	grass.sway_height = 0.6
	blade.material = grass
	blades.multimesh.mesh = blade
	blades.multimesh.instance_count = 20000
	for i in 20000:
		var at := Vector3(rng.randf_range(-5, 5), 0, rng.randf_range(-70, 70))
		blades.multimesh.set_instance_transform(i, Transform3D(Basis(Vector3.UP, rng.randf() * TAU), at))
	world.add_child(blades)

	# Someone walking down the grass, pressing it aside and leaving footprints.
	walker = Node3D.new()
	walker.position = Vector3(0, 0.3, -60)
	var body := MeshInstance3D.new()
	body.mesh = CapsuleMesh.new()
	body.position = Vector3(0, 0.6, 0)
	walker.add_child(body)
	walker.add_child(FoliageBender3D.new())
	var steps := GroundMarks3D.new()
	steps.mode = GroundMarks3D.MODE_STAMPS
	steps.width = 0.15
	steps.spacing = 0.7
	steps.lifetime = 60.0
	steps.emitting = true
	walker.add_child(steps)
	world.add_child(walker)

	camera = Camera3D.new()
	camera.position = Vector3(0, 2, 40)
	world.add_child(camera)

	label = Label.new()
	label.position = Vector2(16, 16)
	root.add_child(label)


func _process(delta: float) -> bool:
	if turning:
		camera.rotation.y += delta * 0.25
		camera.position = Vector3(sin(camera.rotation.y), 0, cos(camera.rotation.y)) * 40.0 + Vector3(0, 2, 0)
	walker.position.z = fmod(walker.position.z + delta * 1.4 + 60.0, 120.0) - 60.0
	walker.position.x = sin(walker.position.z * 0.2) * 1.5
	label.text = "%02d:%02d  moon %.2f  cloud %.2f  rain %.2f  snow %.2f  fog %.2f  wet %.2f  snow cover %.2f  effects %s  grades %s\nR rain, S snow, L storm, F fog, C clouds, A sky, T an hour later, M moon, G grades, E effects, Space camera" % [
		int(day.time), int(fmod(day.time, 1.0) * 60.0), day.moon_phase, weather.get_current_cloud_cover(), weather.get_current_rain(), weather.get_current_snow(),
		weather.get_current_fog(), weather.get_wetness(), weather.get_snow_cover(),
		"on" if compositor.compositor_effects[0].enabled else "off", "on" if grades[0].weight > 0.0 else "off"]
	return false


func _physics_process(_delta: float) -> bool:
	if Input.is_key_pressed(KEY_R) and _once(KEY_R):
		weather.rain = 0.0 if weather.rain > 0.0 else 1.0
		weather.wind = Vector2(4, 1.5) * (3.0 if weather.rain > 0.0 else 1.0)
	if Input.is_key_pressed(KEY_S) and _once(KEY_S):
		weather.snow = 0.0 if weather.snow > 0.0 else 1.0
	if Input.is_key_pressed(KEY_L) and _once(KEY_L):
		var storm := weather.lightning == 0.0
		weather.lightning = 1.0 if storm else 0.0
		weather.rain = 1.0 if storm else weather.rain
		weather.wind = Vector2(4, 1.5) * (4.0 if storm else 1.0)
	if Input.is_key_pressed(KEY_F) and _once(KEY_F):
		weather.fog = 0.0 if weather.fog > 0.0 else 0.8
	if Input.is_key_pressed(KEY_M) and _once(KEY_M):
		day.moon_phase = fmod(day.moon_phase + 0.25, 1.0)
	if Input.is_key_pressed(KEY_G) and _once(KEY_G):
		for grade in grades:
			grade.weight = 0.0 if grade.weight > 0.0 else 1.0
	if Input.is_key_pressed(KEY_C) and _once(KEY_C):
		weather.cloud_cover = 0.1 if weather.cloud_cover > 0.5 else 0.8
	if Input.is_key_pressed(KEY_A) and _once(KEY_A):
		sky.sky_material = CloudySkyMaterial.new() if sky.sky_material is AtmosphereSkyMaterial else AtmosphereSkyMaterial.new()
	if Input.is_key_pressed(KEY_T) and _once(KEY_T):
		day.time = fmod(day.time + 1.0, 24.0)
	if Input.is_key_pressed(KEY_E) and _once(KEY_E):
		for effect: CompositorEffect in compositor.compositor_effects:
			effect.enabled = not effect.enabled
	if Input.is_key_pressed(KEY_SPACE) and _once(KEY_SPACE):
		turning = not turning
	for key in _held.keys():
		if not Input.is_key_pressed(key):
			_held.erase(key)
	return false


var _held := {}


func _once(key: int) -> bool:
	if _held.has(key):
		return false
	_held[key] = true
	return true
