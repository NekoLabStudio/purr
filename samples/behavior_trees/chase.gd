# Runs at the player until it gets away (or is caught).
class_name Chase
extends BehaviorTask

@export var speed := 220.0
@export var gives_up_at := 260.0


func _tick(delta: float) -> Status:
	var player: Node2D = blackboard.get("player")
	if player == null:
		return FAILURE
	var offset: Vector2 = player.global_position - agent.global_position
	if offset.length() > gives_up_at:
		return FAILURE
	if offset.length() < 12.0:
		return SUCCESS
	agent.global_position += offset.normalized() * minf(speed * delta, offset.length())
	blackboard["state"] = "chasing"
	return RUNNING
