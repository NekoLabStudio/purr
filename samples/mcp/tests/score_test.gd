extends TestCase


func test_score_starts_at_three() -> void:
	var main: Node = load("res://main.tscn").instantiate()
	assert_equal(main.score, 3)
	main.free()
