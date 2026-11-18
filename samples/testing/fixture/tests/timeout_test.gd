extends TestCase

var after_finished := false


# Times out after a second, and its wait ends half a second into the next test.
func test_a_slow() -> void:
	await wait_seconds(1.5)


# Its own await mustn't be cut short when the slow one finishes.
func test_b_after() -> void:
	await wait_seconds(0.8)
	after_finished = true


func test_c_check() -> void:
	assert_true(after_finished, "the test before ran to its end")
