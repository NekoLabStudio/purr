# The game, such as it is. Its tests are in tests/; run them with:
#   purr --headless --path samples/testing --run-tests
extends Node


func _ready() -> void:
	var score := Score.new()
	score.add(10)
	print("Score: ", score.points)
