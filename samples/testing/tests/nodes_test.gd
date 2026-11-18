extends TestCase


func test_a_node_moves_over_frames() -> void:
	var body := Node2D.new()
	add_child(body)
	var tween := body.create_tween()
	tween.tween_property(body, "position", Vector2(100, 0), 0.1)
	await wait_seconds(0.2)
	assert_almost_equal(body.position, Vector2(100, 0))


func test_nodes_a_test_adds_are_gone_after_it() -> void:
	assert_equal(get_child_count(), 0, "the last test's node was freed")


func test_physics_ticks() -> void:
	var ticks := [0]
	get_tree().physics_frame.connect(func() -> void: ticks[0] += 1)
	await wait_physics_frames(3)
	assert_greater(ticks[0], 2)
