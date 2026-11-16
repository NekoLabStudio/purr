# Two players matched by rating, playing, then both reporting the result, against a local copy
# of the online service (see test.gd). Each player needs an account of their own, so run each
# from its own copy of this project (with a different config/name, so a different user folder):
#   purr --headless --path <copy A> -s ranked_test.gd -- winner
#   purr --headless --path <copy B> -s ranked_test.gd -- loser
# Each exits non-zero if its check fails.
extends SceneTree

var winner := OS.get_cmdline_user_args().has("winner")
var other_peer := 0
var done := false


func _initialize() -> void:
	run.call_deferred()
	create_timer(60.0).timeout.connect(func(): finish(false, "timed out"))


func run() -> void:
	var signed_in: Dictionary = await Account.sign_in()
	if not signed_in.ok or not Lobbies.can_matchmake():
		finish(false, "not signed in: %s" % signed_in)
		return
	var before: Dictionary = await Lobbies.get_rating("duel")
	get_multiplayer().peer_connected.connect(func(id): other_peer = id)
	var matched: Dictionary = await Lobbies.matchmake("duel", 2)
	if not matched.ok:
		finish(false, "no match: %s" % matched)
		return
	while other_peer == 0:
		await create_timer(0.1).timeout
	# Both games check what they both know, then report the same result: the winner first.
	Lobbies.check("finish", {"laps": 3})
	var checked: Array = await Lobbies.check_completed
	if not checked[1]:
		finish(false, "the check failed: %s" % [checked])
		return
	var me := get_multiplayer().get_unique_id()
	Lobbies.report_result([me, other_peer] if winner else [other_peer, me])
	await Lobbies.result_recorded
	var after: Dictionary = await Lobbies.get_rating("duel")
	var change: float = after.data.rating - before.data.rating
	finish(change > 0 if winner else change < 0, "rating went from %s to %s" % [before.data.rating, after.data.rating])


func finish(ok: bool, detail: String) -> void:
	if done:
		return
	done = true
	print("check: ", "winner" if winner else "loser", "'s rating moved", "" if ok else "  FAILED  " + detail)
	await create_timer(0.5).timeout
	Lobbies.leave()
	quit(0 if ok else 1)
