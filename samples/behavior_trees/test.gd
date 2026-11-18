# Checks behaviour trees. Exits non-zero if a check fails:
#   purr --headless --path samples/behavior_trees -s test.gd
extends SceneTree

var failures := 0


func check(what: String, ok: bool, detail: Variant = null) -> void:
	print("check: ", what, "" if ok else "  FAILED  %s" % [detail])
	if not ok:
		failures += 1


# Notes what happens to it on the blackboard, and returns what it's told to.
class Step extends BehaviorTask:
	@export var label := ""
	@export var results: Array[Status] = [SUCCESS]
	@export var sound: Resource = Resource.new()
	var ticks := 0

	func _setup() -> void:
		_note("setup")

	func _enter() -> void:
		_note("enter")

	func _exit() -> void:
		_note("exit")

	func _tick(_delta: float) -> Status:
		_note("tick")
		ticks += 1
		return results[mini(ticks - 1, results.size() - 1)]

	func _note(what: String) -> void:
		var log: Array = blackboard.get_or_add("log", [])
		log.append("%s %s" % [label, what])


# A state that notes what happens to it, and moves on when its blackboard says to.
class Noted extends BehaviorState:
	func _setup() -> void:
		_note("setup")

	func _enter() -> void:
		_note("enter")
		var next: String = blackboard.get("on_enter_" + name, "")
		if next != "":
			blackboard.erase("on_enter_" + name)
			transition_to(next)

	func _exit() -> void:
		_note("exit")

	func _update(_delta: float) -> void:
		_note("update")
		var next: String = blackboard.get("next_" + name, "")
		if next != "":
			blackboard.erase("next_" + name)
			transition_to(next)

	func _note(what: String) -> void:
		var log: Array = blackboard.get_or_add("log", [])
		log.append("%s %s" % [name, what])


func state(named: String, parent: Node) -> Noted:
	var made := Noted.new()
	made.name = named
	parent.add_child(made)
	return made


class Guard extends Node:
	var calls: Array = []

	func shout(word: String) -> bool:
		calls.append(word)
		return word != "no"


func step(label: String, results: Array[BehaviorTask.Status] = [BehaviorTask.SUCCESS]) -> Step:
	var task := Step.new()
	task.label = label
	task.results = results
	return task


func node_with(type: BehaviorTask, children: Array[BehaviorTask]) -> BehaviorTask:
	type.children = children
	return type


# Runs a tree for an agent by hand, tick by tick.
func player_for(root: BehaviorTask, agent: Node = null, values := {}) -> BehaviorTreePlayer:
	var tree := BehaviorTree.new()
	tree.root = root
	tree.blackboard = values
	var player := BehaviorTreePlayer.new()
	player.update_mode = BehaviorTreePlayer.UPDATE_MANUAL
	player.behavior_tree = tree
	var holder := agent if agent != null else Node.new()
	holder.add_child(player)
	get_root().add_child(holder)
	return player


func log_of(player: BehaviorTreePlayer) -> Array:
	return player.blackboard.get("log", [])


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	const S := BehaviorTask.SUCCESS
	const F := BehaviorTask.FAILURE
	const R := BehaviorTask.RUNNING

	var sequence := player_for(node_with(BehaviorSequence.new(), [step("a"), step("b", [F]), step("c")]))
	check("a sequence fails when a child fails", sequence.update(0.1) == F)
	check("and doesn't run the rest", log_of(sequence) == ["a setup", "b setup", "c setup", "a enter", "a tick", "a exit", "b enter", "b tick", "b exit"], log_of(sequence))

	var selector := player_for(node_with(BehaviorSelector.new(), [step("a", [F]), step("b"), step("c")]))
	check("a selector succeeds with the first child that does", selector.update(0.1) == S)
	check("and goes no further", not log_of(selector).has("c tick"), log_of(selector))

	var resumes := player_for(node_with(BehaviorSequence.new(), [step("a"), step("b", [R, R, S])]))
	check("a running child keeps the sequence running", resumes.update(0.1) == R)
	resumes.update(0.1)
	check("and the sequence resumes at it, not from the start", resumes.update(0.1) == S and log_of(resumes).count("a tick") == 1, log_of(resumes))
	check("a task enters once and leaves once however long it runs", log_of(resumes).count("b enter") == 1 and log_of(resumes).count("b exit") == 1, log_of(resumes))

	var any := BehaviorParallel.new()
	var racing := player_for(node_with(any, [step("slow", [R]), step("quick", [R, S])]))
	racing.update(0.1)
	check("a parallel finishing on any finishes with the first", racing.update(0.1) == S)
	check("stopping the others", log_of(racing).has("slow exit"), log_of(racing))
	var all := BehaviorParallel.new()
	all.finish_when = BehaviorParallel.FINISH_WHEN_ALL
	var both := player_for(node_with(all, [step("a", [R, S]), step("b", [R, R, F])]))
	both.update(0.1)
	both.update(0.1)
	check("one waiting for all keeps going while any runs", both.get_last_status() == R)
	check("and fails if one failed", both.update(0.1) == F)

	check("an inverter turns success into failure", player_for(node_with(BehaviorInverter.new(), [step("a")])).update(0.1) == F)

	var repeat := BehaviorRepeat.new()
	repeat.times = 3
	var thrice := player_for(node_with(repeat, [step("a")]))
	check("a repeat runs while it has times left", thrice.update(0.1) == R and thrice.update(0.1) == R)
	check("and succeeds after the last", thrice.update(0.1) == S and log_of(thrice).count("a tick") == 3, log_of(thrice))
	var failing_repeat := BehaviorRepeat.new()
	check("a child failing stops a repeat", player_for(node_with(failing_repeat, [step("a", [F])])).update(0.1) == F)

	var wait := BehaviorWait.new()
	wait.seconds = 0.25
	var waiting := player_for(wait)
	check("a wait runs for its seconds", waiting.update(0.1) == R and waiting.update(0.1) == R and waiting.update(0.1) == S)

	var limit := BehaviorTimeLimit.new()
	limit.seconds = 0.15
	var limited := player_for(node_with(limit, [step("forever", [R])]))
	check("a time limit lets its child run", limited.update(0.1) == R)
	check("then stops it and fails", limited.update(0.1) == F and log_of(limited).has("forever exit"), log_of(limited))

	var cooldown := BehaviorCooldown.new()
	cooldown.seconds = 0.2
	var cooling := player_for(node_with(cooldown, [step("fire")]))
	check("a cooldown runs its child", cooling.update(0.1) == S)
	check("then fails without running it", cooling.update(0.1) == F and log_of(cooling).count("fire tick") == 1, log_of(cooling))
	check("until its seconds have passed", cooling.update(0.1) == S)

	var guard := Guard.new()
	var call := BehaviorCallMethod.new()
	call.method = &"shout"
	call.arguments = ["halt"]
	var calling := player_for(call, guard)
	check("call method calls the agent's method", calling.update(0.1) == S and guard.calls == ["halt"], guard.calls)
	(calling.get_running_tree() as BehaviorCallMethod).arguments = ["no"]
	check("and fails when it returns false", calling.update(0.1) == F)

	# Copies: two agents with the same tree keep their own task state.
	var shared := BehaviorTree.new()
	shared.root = node_with(BehaviorSequence.new(), [step("a"), step("b", [R, S])])
	shared.blackboard = {"speed": 3}
	var first := BehaviorTreePlayer.new()
	first.update_mode = BehaviorTreePlayer.UPDATE_MANUAL
	first.behavior_tree = shared
	first.blackboard = {"speed": 5}
	var second := BehaviorTreePlayer.new()
	second.update_mode = BehaviorTreePlayer.UPDATE_MANUAL
	second.behavior_tree = shared
	get_root().add_child(first)
	get_root().add_child(second)
	first.update(0.1)
	check("each player runs its own copy", second.update(0.1) == R and (second.get_running_tree().get_child(1) as Step).ticks == 1)
	check("the tree's own tasks are untouched", (shared.root.get_child(1) as Step).ticks == 0)
	var copy := second.get_running_tree().get_child(1) as Step
	var original := shared.root.get_child(1) as Step
	check("a copy's arrays are its own", not is_same(copy.results, original.results) and copy.results == original.results)
	check("other resources its tasks hold are shared, not copied", is_same(copy.sound, original.sound))
	check("a player's blackboard values win over the tree's", first.blackboard["speed"] == 5 and second.blackboard["speed"] == 3, [first.blackboard, second.blackboard])
	check("and the game sees what tasks write", first.blackboard.has("log"))

	var finished: Array = []
	first.finished.connect(finished.append)
	first.update(0.1)
	check("the player says when the tree finishes", finished == [S], finished)
	first.update(0.1)
	check("and starts it again", log_of(first).count("a tick") == 2, log_of(first))

	var stopped := player_for(step("long", [R]))
	stopped.update(0.1)
	stopped.active = false
	check("turning a player off stops its tree", log_of(stopped).has("long exit"), log_of(stopped))

	var ticking := player_for(step("auto", [R]))
	ticking.update_mode = BehaviorTreePlayer.UPDATE_PHYSICS
	for i in 3:
		await physics_frame
	check("a player ticks with physics by default", log_of(ticking).count("auto tick") >= 2, log_of(ticking))

	await check_state_machines()

	print("behavior trees: %d failed" % failures if failures else "behavior trees: all passed")
	quit(1 if failures else 0)


func check_state_machines() -> void:
	var guard := Node.new()
	guard.name = "Guard"
	var machine := BehaviorStateMachine.new()
	machine.update_mode = BehaviorStateMachine.UPDATE_MANUAL
	guard.add_child(machine)
	state("Patrol", machine)
	var chase := BehaviorStateMachine.new()
	chase.name = "Chase"
	machine.add_child(chase)
	state("Run", chase)
	state("Strike", chase)
	state("Search", machine)
	var changes: Array = []
	machine.state_changed.connect(func(from: BehaviorState, to: BehaviorState) -> void:
		changes.append([String(from.name) if from else "", String(to.name)]))
	get_root().add_child(guard)
	var log: Array = machine.blackboard.get("log", [])

	check("a machine starts in its first state", machine.get_current_state().name == "Patrol" and log.has("Patrol enter"), log)
	check("every state is set up first, once", log.slice(0, 4) == ["Patrol setup", "Run setup", "Strike setup", "Search setup"], log)
	check("the agent is the machine's parent", (machine.get_current_state() as Noted).agent == guard)
	machine.tick(0.1)
	check("the active state is updated each tick", log.count("Patrol update") == 1 and not log.has("Search update"), log)

	machine.blackboard["next_Patrol"] = "Chase"
	machine.tick(0.1)
	check("a state moves on with transition_to()", machine.get_current_state() == chase, machine.get_current_state())
	check("leaving one state and entering the next", log.slice(-2) == ["Patrol exit", "Run enter"], log)
	check("a machine inside another starts its own first state", chase.get_current_state().name == "Run")
	machine.tick(0.1)
	check("and updates it while it's active", log.has("Run update"), log)
	check("the machine says what changed", changes.has(["Patrol", "Chase"]), changes)

	chase.add_transition("Run", "Strike", "in_reach")
	machine.add_transition("", "Search", "lost_sight")
	machine.send_event("in_reach")
	check("an event takes the deepest machine's transition", chase.get_current_state().name == "Strike", chase.get_current_state())
	(chase.get_current_state() as BehaviorState).send_event("lost_sight")
	check("one its machine doesn't know goes to the machine round it", machine.get_current_state().name == "Search", machine.get_current_state())
	check("leaving the inner machine leaves its state too", log.has("Strike exit") and log.slice(-1) == ["Search enter"], log)

	var allowed := [false]
	machine.add_transition("Search", "Patrol", "give_up", func() -> bool: return allowed[0])
	machine.send_event("give_up")
	check("a guard can refuse a transition", machine.get_current_state().name == "Search")
	allowed[0] = true
	machine.send_event("give_up")
	check("and allow it", machine.get_current_state().name == "Patrol")

	changes.clear()
	machine.blackboard["on_enter_Search"] = "Patrol"
	machine.change_state("Search")
	check("moving on from _enter() waits for the change it's in", log.slice(-4) == ["Patrol exit", "Search enter", "Search exit", "Patrol enter"], log.slice(-4))
	check("so the changes are told in order", changes == [["Patrol", "Search"], ["Search", "Patrol"]], changes)

	get_root().remove_child(guard)
	check("taken out of the tree, the machine stops", not machine.is_active() and log.slice(-1) == ["Patrol exit"], log.slice(-2))
	get_root().add_child(guard)
	check("put back, it starts again", machine.is_active() and machine.get_current_state().name == "Patrol" and log.slice(-1) == ["Patrol enter"], log.slice(-2))

	guard.queue_free()
	await process_frame
	check("leaving the tree exits the active state", log.slice(-1) == ["Patrol exit"], log.slice(-3))
