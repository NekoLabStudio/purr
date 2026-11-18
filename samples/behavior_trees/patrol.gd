# Walks to the next of the blackboard's patrol points.
class_name Patrol
extends BehaviorTask

@export var speed := 90.0
var next := 0


func _tick(delta: float) -> Status:
	var points: Array = blackboard.get("patrol_points", [])
	if points.is_empty():
		return FAILURE
	var target: Vector2 = points[next % points.size()]
	var offset: Vector2 = target - agent.global_position
	blackboard["state"] = "patrolling"
	if offset.length() < 2.0:
		next += 1
		return SUCCESS
	agent.global_position += offset.normalized() * minf(speed * delta, offset.length())
	return RUNNING
