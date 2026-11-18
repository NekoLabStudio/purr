extends TestCase

var score: Score


func _before_each() -> void:
	score = Score.new()


func test_starts_at_nothing() -> void:
	assert_equal(score.points, 0)


func test_adds_points() -> void:
	score.add(5)
	score.add(3)
	assert_equal(score.points, 8)


func test_keeps_the_best_after_a_reset() -> void:
	score.add(12)
	score.reset()
	score.add(4)
	assert_equal(score.best, 12, "the best score outlives a reset")


func test_says_when_there_is_a_new_best() -> void:
	var bests: Array[int] = []
	score.new_best.connect(bests.append)
	score.add(3)
	score.add(2)
	assert_equal(bests, [3, 5])


func test_waits_for_the_signal() -> void:
	(func() -> void: score.add(1)).call_deferred()
	assert_true(await wait_for(score.new_best), "new_best came before the timeout")


func test_adding_is_quick() -> void:
	var timed: Dictionary = await measure("adding points", func():
		for i in 1000:
			score.add(1), 30)
	assert_less(timed.median_ms, 50.0, "a thousand points in under 50 ms")
