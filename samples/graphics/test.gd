# Checks FrameBudget. Exits non-zero if a check fails:
#   purr --headless --path samples/graphics -s test.gd
extends SceneTree

var failures := 0
var steps := 0
var long_frames: Array[Dictionary] = []


func check(what: String, ok: bool, detail: Variant = null) -> void:
	print("check: ", what, "" if ok else "  FAILED  %s" % [detail])
	if not ok:
		failures += 1


func busy(milliseconds: float) -> void:
	var until := Time.get_ticks_usec() + int(milliseconds * 1000.0)
	while Time.get_ticks_usec() < until:
		pass


func frames(count: int) -> void:
	for i in count:
		await process_frame


func step_slowly() -> bool:
	steps += 1
	busy(0.25)
	return steps < 1000


func step_once() -> void:
	steps += 1


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	check("quiet headless, where frames draw nothing", not FrameBudget.reporting)
	FrameBudget.reporting = true
	FrameBudget.long_frame.connect(func(milliseconds: float, work: Dictionary) -> void:
		long_frames.append({"ms": milliseconds, "work": work}))

	# A long frame says what took its time.
	await frames(2)
	FrameBudget.long_frame_ms = 15.0
	var started := Time.get_ticks_usec()
	busy(25.0)
	FrameBudget.measure("pathfinding", started)
	await frames(2)
	var long: Dictionary = long_frames.back() if not long_frames.is_empty() else {}
	check("a long frame is reported with its measured work", long.get("ms", 0.0) >= 25.0 and long.get("work", {}).get("pathfinding", 0.0) >= 24.0, long)
	# Far above the odd stall a busy machine (a CI runner) has with nothing running.
	FrameBudget.long_frame_ms = 250.0
	long_frames.clear()
	await frames(3)
	check("short frames aren't", long_frames.is_empty(), long_frames)
	FrameBudget.long_frame_ms = 1000.0

	# Spread work runs within its budget each frame, until its step says it's done.
	steps = 0
	FrameBudget.spread("building", step_slowly, 2.0)
	check("spread work waits for the frame", steps == 0 and FrameBudget.get_spread_count() == 1)
	await frames(1)
	var in_a_frame := steps
	check("a frame's share stays near its budget", in_a_frame >= 2 and in_a_frame <= 12, in_a_frame)
	FrameBudget.screen_hidden = true
	var before := steps
	await frames(1)
	var hidden := steps - before
	FrameBudget.screen_hidden = false
	check("more is done while the screen's hidden", hidden > in_a_frame * 2, [in_a_frame, hidden])
	while FrameBudget.get_spread_count() > 0:
		await frames(1)
	check("the job ends when its step says so", steps == 1000)

	steps = 0
	FrameBudget.spread("once", step_once)
	await frames(2)
	check("a step that returns nothing runs once", steps == 1 and FrameBudget.get_spread_count() == 0)

	var holder := Node.new()
	FrameBudget.spread("gone", holder.queue_free)
	holder.free()
	await frames(1)
	check("a job whose object's gone is dropped", FrameBudget.get_spread_count() == 0)

	print("graphics test: %s" % ("all checks passed" if failures == 0 else "%d failed" % failures))
	quit(failures)
