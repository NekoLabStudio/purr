extends TestCase


func test_passes() -> void:
	assert_true(true)


func test_unequal() -> void:
	assert_equal(2 + 2, 5, "arithmetic")


func test_fails_after_waiting() -> void:
	await wait_frames(2)
	assert_has([1, 2, 3], 4)


func test_script_error() -> void:
	var missing: Object = null
	missing.call("anything")


func test_skipped() -> void:
	skip("not written yet")
