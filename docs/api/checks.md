# Checks and flags

A check compares something every player's game can work out on its own: a lap time, a score, the number of items left, a hash of the game's shared state. If one player's game disagrees with everyone else's, that player's game has probably been tampered with.

```gdscript
func _on_lap_finished(lap: int) -> void:
	Lobbies.check("lap_%d" % lap, {"time": race_time, "position": positions})


func _ready() -> void:
	Lobbies.check_completed.connect(_on_check_completed)


func _on_check_completed(check: String, agreed: bool, disagreeing: Array) -> void:
	if not agreed and multiplayer.is_server():
		for peer_id in disagreeing:
			Lobbies.kick(peer_id)
```

Every player's game calls `Lobbies.check()` with the same name at the same moment in the game. Only a hash of the value leaves the device. Once every player in the lobby has answered, `Lobbies.check_completed` fires on everyone's device: `agreed` is true when every value matched, and `disagreeing` lists the peer IDs of players whose value differs from the majority's. With no majority (two players who disagree, say), nobody is named.

Players named in `disagreeing` are reported as cheating to the online services' operator automatically (see [Moderation](../online/moderation.md)), unless the operator sets `report_failed_checks` to `false` for the game. With `kick_cheaters` on for the game, the service also removes them from the lobby (see [What happens to a cheater](#what-happens-to-a-cheater)). In a [ranked](ranked.md) lobby, a failed check stops the match's result from changing anyone's rating.

The game decides what to check and what to do about a failed check: remove the player, end the match, or only note it. Each name is checked once per lobby, and a lobby holds up to 256 checks, so check at moments that matter (a lap, a round, the end of a match) rather than every frame.

## Flags

Some cheats can't be checked by comparing values. Each player sees the others move a little differently, because of latency and smoothing, so positions never match exactly. Each game can still tell when another player does something impossible, and flag them:

```gdscript
func _on_player_moved(peer_id: int, from: Vector3, to: Vector3, seconds: float) -> void:
	if from.distance_to(to) > MAX_SPEED * seconds * 1.5:
		Lobbies.flag(peer_id, "teleport")


func _ready() -> void:
	Lobbies.player_flagged.connect(func(peer_id, reason): print(peer_id, " was flagged: ", reason))
```

The game's own rule decides what's impossible, with whatever allowance its latency needs. When more than half of the other players, and at least two of them, flag the same player for the same reason within 10 seconds, `player_flagged` fires on everyone's device, and the service handles it like a failed check. One player can't get another flagged alone, so a lobby of two never confirms a flag.

Flags suit anything other players can see: teleporting, moving too fast, hits through walls, health that doesn't go down.

## What happens to a cheater

The online services' operator sets what happens, for each game in the service's `GAMES` settings:

| Setting | Effect |
|---|---|
| `report_failed_checks` | `true` by default: players named by a failed check or a confirmed flag are reported as cheating |
| `kick_cheaters` | `false` by default: with `true`, they're also removed from the lobby, as if the host had removed them. A cheating host is removed too, and the player who has been there longest takes over. |

Removals for cheating can lead to a ban from multiplayer. With the service's `AUTO_BAN` setting, a number of removals within a window bans the player, for longer each time and in the end for good (see [Moderation](../online/moderation.md#automatic-bans)).

The game can also act on `check_completed` and `player_flagged` itself, for example by ending the match.

## What checks and flags can catch

Checks and flags catch a player whose game does or reports something the others can see is wrong: a lap time faster than the race they all watched, a score the others didn't count, a resource total that doesn't add up. Choose values every player's game knows from the shared game, and leave out what only one player's game can know, such as their own input or what's hidden from the others.

They can't catch a change that only affects what one player sees, such as seeing through walls, and players who team up can agree on a false value. A player who leaves before answering doesn't count. For a game where cheating must be impossible, the game has to run on a server the players don't control, which Purr doesn't provide.
