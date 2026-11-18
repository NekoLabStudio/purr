# Succeeds when the player is within reach.
class_name PlayerNear
extends BehaviorTask

@export var reach := 180.0


func _tick(_delta: float) -> Status:
	var player: Node2D = blackboard.get("player")
	if player != null and agent.global_position.distance_to(player.global_position) < reach:
		return SUCCESS
	return FAILURE
