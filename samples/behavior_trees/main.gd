# A guard walks its rounds, pausing at each corner, and chases the pointer when it comes near.
# Its behaviour is guard_tree.tres: open it in the inspector to see the tasks.
extends Node2D

@onready var guard: Node2D = $Guard
@onready var brain: BehaviorTreePlayer = $Guard/BehaviorTreePlayer
var player := Node2D.new()


func _ready() -> void:
	add_child(player)
	brain.blackboard["player"] = player


func _process(_delta: float) -> void:
	player.global_position = get_global_mouse_position()
	var points: Array = brain.blackboard["patrol_points"]
	DebugDraw.path_2d(PackedVector2Array(points), Color(1, 1, 1, 0.3), 0.0, true)
	DebugDraw.circle_2d(guard.global_position, 14.0, Color.ORANGE)
	DebugDraw.circle_2d(guard.global_position, 180.0, Color(1, 0.6, 0.2, 0.25))
	DebugDraw.circle_2d(player.global_position, 8.0, Color.SKY_BLUE)
	DebugDraw.show_value("guard", brain.blackboard["state"])
