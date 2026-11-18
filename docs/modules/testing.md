# Testing

Purr runs your game's tests and checks your project for broken files, from the command line, so both can run in CI.

```sh
purr --headless --run-tests
purr --headless --check-project
```

Both exit with 1 when something fails, and print only what failed and a summary.

## Tests

Put test scripts in a `tests` folder at the root of the project, named so they end in `_test.gd` (or start with `test_`). A test script extends `TestCase`, and every method whose name starts with `test_` is a test:

```gdscript
extends TestCase

var score: Score


func _before_each() -> void:
	score = Score.new()


func test_adds_points() -> void:
	score.add(5)
	score.add(3)
	assert_equal(score.points, 8)


func test_keeps_the_best_after_a_reset() -> void:
	score.add(12)
	score.reset()
	assert_equal(score.best, 12, "the best score outlives a reset")
```

A failure says where it was and what went wrong:

```
FAIL  res://tests/score_test.gd:14  test_keeps_the_best_after_a_reset: the best score outlives a reset (expected 12, got 0)
Tests: 7 passed, 1 failed (0.2 s).
```

The assertions are `assert_true()`, `assert_false()`, `assert_equal()`, `assert_not_equal()`, `assert_almost_equal()` (for floats, vectors, colours and transforms), `assert_greater()`, `assert_less()`, `assert_null()`, `assert_not_null()` and `assert_has()` (an item in an array, a key in a dictionary or text in a string). Each takes an optional message saying what's being checked. A failed assertion doesn't stop the test, so one run shows every failure. `fail()` fails the test outright and `skip()` marks it as skipped.

Tests run in the order they're written. `_before_each()` and `_after_each()` run round each test, and `_before_all()` and `_after_all()` round the whole script. A script error fails the test it happens in, and the others carry on.

### Waiting

The test script is a node in the scene tree, so tests can add nodes, let them run and await what happens. Nodes a test adds as its children are freed when it ends.

```gdscript
func test_the_door_opens_when_the_player_is_near() -> void:
	var door := preload("res://door.tscn").instantiate()
	add_child(door)
	door.player_entered(Vector3.ZERO)
	assert_true(await wait_for(door.opened), "the door opened")


func test_the_lift_arrives() -> void:
	var lift := Lift.new()
	add_child(lift)
	lift.call_to(3)
	await wait_seconds(2.0)
	assert_almost_equal(lift.position.y, 9.0)
```

`wait_frames()`, `wait_physics_frames()` and `wait_seconds()` wait as they say. `wait_for()` waits for a signal, giving `true` when it came and `false` if it didn't within its timeout (5 seconds unless you say). Any other `await` works too. A test that hasn't finished after 30 seconds fails (change that with `testing/timeout_seconds` in Project Settings).

### Measuring

`measure()` times your game's heavy work, so you can see what a change did to it. It calls a function once a frame (120 times unless you say) and times only that call, not the rest of the frame:

```gdscript
func test_traffic_keeps_up() -> void:
	var city := preload("res://city.tscn").instantiate()
	add_child(city)
	await wait_seconds(5.0) # let the streets fill
	city.traffic.set_physics_process(false)
	var timed: Dictionary = await measure("traffic", func(): city.traffic._physics_process(1.0 / 60.0))
	assert_less(timed.median_ms, 2.0, "traffic within 2 ms a frame")
```

Each measure prints the middle call's time, the time 95 calls in 100 come within, and the slowest call. It also compares with the last run on the same computer, so run the tests before and after a change:

```
TIME  res://tests/city_test.gd traffic: 0.44 ms a call, 95% within 0.61 ms, slowest 1.2 ms (20% faster than last run's 0.55 ms)
```

Changes under 15% read as "about the same", since timings vary that much between runs. The result has `median_ms`, `p95_ms`, `worst_ms` and, after the first run, `last_run_ms`, for a budget a test can check. Keep budgets generous, since CI machines are slower and busier than yours. The function shouldn't await: only its time up to the first `await` is counted.

### Choosing what runs

`--run-tests=score` runs only test files whose paths contain `score`, or, when no file does, only the tests whose names contain it. A filter that matches nothing fails, so a typo doesn't pass quietly.

`--test-report=results.xml` also writes the results as a JUnit report, which most CI systems can show.

## Checking the project

`--check-project` goes through every file in the project and reports:

- scripts that don't compile,
- scenes and resources that don't load, or that need a file that isn't there,
- a main scene or autoload that isn't there,
- nodes' configuration warnings, the ones the editor shows beside nodes in the scene tree.

```
ERROR    res://levels/forest.tscn: needs res://art/tree.png, which isn't there.
WARNING  res://scenes/car.tscn: Car/Collision: A shape must be provided for CollisionShape3D to function.
Checked 379 files: 1 error, 1 warning.
```

Errors fail the check; warnings don't. Scenes are made into nodes to check them, without joining the scene tree, so `_init()` runs but `_ready()` doesn't. `--test-report` works here too.

## In CI

Import the project first, so classes made with `class_name` and imported files are there, then run both:

```sh
purr --headless --import
purr --headless --check-project --test-report=check.xml
purr --headless --run-tests --test-report=tests.xml
```

## Project settings

| Setting | Default | What it does |
| --- | --- | --- |
| `testing/tests_folder` | `res://tests` | Where test scripts are. Folders inside it are searched too. |
| `testing/check_excludes` | `res://addons` | Folders `--check-project` leaves out, such as other people's add-ons. |
