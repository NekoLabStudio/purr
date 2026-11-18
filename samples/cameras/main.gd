# A ball runs round a yard with a wall in it. Three virtual cameras watch it: 1 orbits behind
# (pulled in front of the wall when it's in the way), 2 follows from above, 3 slides along a
# rail. Press 1, 2 or 3 to switch (the camera blends over), the arrow keys to orbit, and Space to
# shake.
extends Node3D

var ball: MeshInstance3D
var orbit: VirtualCamera3D
var above: VirtualCamera3D
var rail: VirtualCamera3D
var time := 0.0


func _ready() -> void:
	var environment := WorldEnvironment.new()
	environment.environment = Environment.new()
	environment.environment.background_mode = Environment.BG_SKY
	environment.environment.sky = Sky.new()
	environment.environment.sky.sky_material = ProceduralSkyMaterial.new()
	add_child(environment)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50, 30, 0)
	sun.shadow_enabled = true
	add_child(sun)

	var ground := MeshInstance3D.new()
	ground.mesh = PlaneMesh.new()
	ground.mesh.size = Vector2(40, 40)
	add_child(ground)

	var wall := StaticBody3D.new()
	wall.position = Vector3(0, 1.5, 0)
	var wall_shape := CollisionShape3D.new()
	wall_shape.shape = BoxShape3D.new()
	wall_shape.shape.size = Vector3(8, 3, 0.4)
	wall.add_child(wall_shape)
	var wall_mesh := MeshInstance3D.new()
	wall_mesh.mesh = BoxMesh.new()
	wall_mesh.mesh.size = wall_shape.shape.size
	wall.add_child(wall_mesh)
	add_child(wall)

	ball = MeshInstance3D.new()
	ball.mesh = SphereMesh.new()
	ball.name = "Ball"
	add_child(ball)

	add_child(Camera3D.new())

	orbit = VirtualCamera3D.new()
	orbit.name = "Orbit"
	orbit.follow_mode = VirtualCamera3D.FOLLOW_ORBIT
	orbit.follow_offset = Vector3(0, 0.5, 0)
	orbit.orbit_distance = 6.0
	orbit.follow_damping = 0.2
	orbit.priority = 1
	add_child(orbit)
	orbit.follow_target = orbit.get_path_to(ball)

	above = VirtualCamera3D.new()
	above.name = "Above"
	above.follow_mode = VirtualCamera3D.FOLLOW_TARGET
	above.follow_offset = Vector3(0, 14, 6)
	above.follow_damping = 0.5
	above.rotation_degrees = Vector3(-65, 0, 0)
	add_child(above)
	above.follow_target = above.get_path_to(ball)

	var path := Path3D.new()
	path.curve = Curve3D.new()
	for x in [-12, -4, 4, 12]:
		path.curve.add_point(Vector3(x, 3, 10))
	add_child(path)
	rail = VirtualCamera3D.new()
	rail.name = "Rail"
	rail.follow_mode = VirtualCamera3D.FOLLOW_PATH
	rail.follow_damping = 0.3
	rail.look_damping = 0.2
	add_child(rail)
	rail.follow_target = rail.get_path_to(ball)
	rail.follow_path = rail.get_path_to(path)


func _process(delta: float) -> void:
	time += delta
	ball.position = Vector3(sin(time * 0.6) * 9.0, 0.5, cos(time * 0.6) * 5.0)
	if Input.is_key_pressed(KEY_LEFT):
		orbit.orbit_yaw -= delta * 2.0
	if Input.is_key_pressed(KEY_RIGHT):
		orbit.orbit_yaw += delta * 2.0
	if Input.is_key_pressed(KEY_UP):
		orbit.orbit_pitch -= delta
	if Input.is_key_pressed(KEY_DOWN):
		orbit.orbit_pitch += delta


func _unhandled_key_input(event: InputEvent) -> void:
	if not event.is_pressed():
		return
	match event.keycode:
		KEY_1, KEY_2, KEY_3:
			for camera: VirtualCamera3D in [orbit, above, rail]:
				camera.priority = 0
			[orbit, above, rail][event.keycode - KEY_1].priority = 1
		KEY_SPACE:
			for camera: VirtualCamera3D in [orbit, above, rail]:
				if camera.is_live():
					camera.shake(1.0, 0.6)
