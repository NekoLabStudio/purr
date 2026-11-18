# A score that counts points and says when it reaches a new best.
class_name Score
extends RefCounted

signal new_best(points: int)

var points := 0
var best := 0


func add(amount: int) -> void:
	points += amount
	if points > best:
		best = points
		new_best.emit(best)


func reset() -> void:
	points = 0
