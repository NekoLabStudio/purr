# Balls circle a post. DebugDraw shows each one's path, velocity and name, and lists values in
# the corner; the console (backquote) has commands to change them: try "help", "speed 3" and
# "balls 12".
extends Node3D

var speed := 1.0
var count := 5
var time := 0.0


func _ready() -> void:
	var camera := Camera3D.new()
	camera.position = Vector3(0, 6, 9)
	add_child(camera)
	camera.look_at(Vector3.ZERO)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-50, 30, 0)
	add_child(light)
	Console.add_command("speed", set_speed, "Sets how fast the balls go round.")
	Console.add_command("balls", set_count, "Sets how many balls there are.")
	Console.add_command("trail", leave_trail, "Leaves each ball's position marked for a few seconds.")


func set_speed(value: float) -> void:
	speed = value


func set_count(value: int) -> void:
	count = clampi(value, 1, 100)


func leave_trail(seconds := 3.0) -> void:
	for i in count:
		DebugDraw.sphere(ball_position(i), 0.2, Color.ORANGE, seconds)


func ball_position(index: int) -> Vector3:
	var angle := time * speed + TAU * index / count
	return Vector3(cos(angle) * 3.0, 0.5 + sin(time * 2.0 + index) * 0.3, sin(angle) * 3.0)


func _process(delta: float) -> void:
	time += delta
	DebugDraw.axes(Transform3D.IDENTITY, 1.0)
	DebugDraw.line(Vector3.ZERO, Vector3(0, 2, 0), Color.GRAY)
	for i in count:
		var at := ball_position(i)
		var ahead := ball_position(i) + Vector3(-sin(time * speed + TAU * i / count), 0, cos(time * speed + TAU * i / count)) * speed
		DebugDraw.sphere(at, 0.25, Color.SKY_BLUE)
		DebugDraw.arrow(at, ahead, Color.YELLOW_GREEN)
		DebugDraw.text(at + Vector3(0, 0.5, 0), "ball %d" % (i + 1))
	DebugDraw.show_value("speed", speed)
	DebugDraw.show_value("balls", count)
	DebugDraw.show_value("fps", Engine.get_frames_per_second())
