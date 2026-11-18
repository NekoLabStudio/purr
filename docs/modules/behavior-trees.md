# Behaviour trees and state machines

A behaviour tree decides what a character does: patrol until it sees the player, then chase, then attack when close, and give up after a while. Each step is a task, and each tick a running task says whether it's still going, succeeded or failed. Tasks with children decide which child runs next.

```
Selector
├── Sequence           (attack when the player's close)
│   ├── Is Player Near
│   └── Attack
├── Sequence           (chase when it can see them)
│   ├── Can See Player
│   └── Chase
└── Patrol             (otherwise)
```

## Running a tree

Add a `BehaviorTreePlayer` as a child of the character and give it a `BehaviorTree`. The character is the tree's agent (set `agent` for another node). The player ticks the tree every physics tick, and starts it again whenever it finishes, so it keeps deciding.

Selecting the player (or a saved tree) opens the Behavior Tree dock at the bottom of the editor, with the tree as an outline. Add Task adds any built-in task, or a script extending `BehaviorTask`, under the selected one (or as the root of an empty tree). Move tasks with the arrows, or drag one onto another task to make it a child, or between two to put it beside them. Selecting a task shows its settings in the inspector. Every change can be undone.

While the game runs from the editor, the dock shows what the tree is doing: each task's last result beside it (running, succeeded or failed), with the running ones highlighted. When several characters run the tree, choose whose to show at the top right. This only happens while the dock shows the tree, and not at all in exported games.

Save the tree as a resource to share it between characters. Each player runs its own copy, so what one guard's tasks remember is theirs alone.

## Writing tasks

Actions and conditions are scripts that extend `BehaviorTask` and override `_tick()`, returning `RUNNING`, `SUCCESS` or `FAILURE`:

```gdscript
class_name Chase
extends BehaviorTask

@export var speed := 4.0


func _tick(delta: float) -> Status:
	var target: Node3D = blackboard.get("target")
	if target == null:
		return FAILURE
	var offset: Vector3 = target.global_position - agent.global_position
	if offset.length() < 1.5:
		return SUCCESS
	agent.velocity = offset.normalized() * speed
	agent.move_and_slide()
	return RUNNING
```

A condition is a task that succeeds or fails at once:

```gdscript
class_name CanSeePlayer
extends BehaviorTask


func _tick(_delta: float) -> Status:
	return SUCCESS if agent.can_see(blackboard.get("target")) else FAILURE
```

`agent` is the node the tree controls and `blackboard` is a dictionary every task in the tree shares. `_setup()` runs once when the player starts, `_enter()` each time the task starts running and `_exit()` when it stops (finished, or stopped by its parent), and `get_elapsed_time()` says how long it's been running.

## The blackboard

The blackboard holds what the tasks share: the target, a point to go to, how alert the guard is. A tree's `blackboard` has its starting values, and a player's `blackboard` overrides them for that character. The game reads and writes the same dictionary:

```gdscript
func _on_vision_spotted(body: Node3D) -> void:
	$BehaviorTreePlayer.blackboard["target"] = body
```

## Built-in tasks

| Task | What it does |
| --- | --- |
| `BehaviorSequence` | Runs its children in order while they succeed. Fails when one fails. |
| `BehaviorSelector` | Tries its children in order until one succeeds. Fails if they all fail. |
| `BehaviorParallel` | Runs all its children at once, finishing when the first does or when they all have. |
| `BehaviorInverter` | Swaps its child's success and failure. |
| `BehaviorRepeat` | Runs its child again each time it finishes, a number of times or for ever. |
| `BehaviorCooldown` | Fails without running its child until some seconds after it last finished. |
| `BehaviorTimeLimit` | Stops its child and fails if it runs too long. |
| `BehaviorWait` | Runs for some seconds, then succeeds. |
| `BehaviorCallMethod` | Calls a method on the agent. Fails if it returns `false`. |

A running task carries on from where it was on the next tick: a sequence whose second child is still running doesn't start again from the first.

## Ticking

`update_mode` says when the player ticks: every physics tick (`UPDATE_PHYSICS`, the default, since most tasks move bodies), every frame (`UPDATE_PROCESS`), or only when the game calls `update()` (`UPDATE_MANUAL`). Turning `active` off stops the tree, telling the running tasks they're leaving. `restart()` starts it again from the beginning, and `finished` is emitted each time the tree finishes.

## State machines

When a character is always in exactly one of a few modes (patrolling, chasing, searching), a state machine can be simpler than a tree. Add a `BehaviorStateMachine` as a child of the character, with a `BehaviorState` child for each state:

```
Guard
└── BehaviorStateMachine
    ├── Patrol
    ├── Chase
    └── Search
```

The machine starts in `initial_state` (or its first state) and updates the active state every physics tick. A state is a script extending `BehaviorState`:

```gdscript
extends BehaviorState


func _enter() -> void:
	agent.play("run")


func _update(delta: float) -> void:
	var target: Node3D = blackboard.get("target")
	if target == null:
		transition_to("Search")
		return
	agent.chase(target, delta)
```

`agent` is the machine's parent and `blackboard` the machine's dictionary, shared by every state. `_enter()` and `_exit()` run as the machine switches, and `_setup()` once before it starts.

States can switch themselves with `transition_to()`, or the game can send events and let the machine decide:

```gdscript
func _ready() -> void:
	$BehaviorStateMachine.add_transition("Patrol", "Chase", "spotted")
	$BehaviorStateMachine.add_transition("", "Flee", "hurt")  # From any state.


func _on_vision_spotted(body: Node3D) -> void:
	$BehaviorStateMachine.blackboard["target"] = body
	$BehaviorStateMachine.send_event("spotted")
```

A transition can have a guard, a callable that has to return `true` for it to happen. A machine is a state too, so machines nest: a `Combat` machine with `Strike` and `Dodge` states can be one state of the guard's machine. Its states run while it's active, and events go to the deepest active machine first, then outwards. `state_changed` is emitted on every switch.
