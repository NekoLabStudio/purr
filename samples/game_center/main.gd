extends VBoxContainer
## Signs in to Game Center and reports an achievement and a score. Export it for macOS, iOS or
## tvOS with a bundle ID set up for Game Center in App Store Connect, and tick
## Entitlements > Game Center in the export preset. The achievement and leaderboard IDs below
## are placeholders: use ones from your game's App Store Connect record.

const ACHIEVEMENT_ID := "first_steps"
const LEADERBOARD_ID := "high_scores"

var status := Label.new()


func _ready() -> void:
	add_child(status)
	if not Engine.has_singleton("GameCenter"):
		status.text = "Game Center is only on macOS, iOS and tvOS."
		return
	status.text = "Signing in..."
	var signed_in: Dictionary = await GameCenter.authenticate()
	if not signed_in.ok:
		status.text = "Not signed in: %s" % signed_in.error
		return
	status.text = "Signed in as %s." % signed_in.player_name

	var achievement := Button.new()
	achievement.text = "Unlock an achievement"
	achievement.pressed.connect(_report_achievement)
	add_child(achievement)

	var score := Button.new()
	score.text = "Submit a score"
	score.pressed.connect(_submit_score)
	add_child(score)


func _report_achievement() -> void:
	var result: Dictionary = await GameCenter.report_achievement(ACHIEVEMENT_ID)
	status.text = "Achievement reported." if result.ok else "Couldn't report it: %s" % result.error


func _submit_score() -> void:
	var result: Dictionary = await GameCenter.submit_score(LEADERBOARD_ID, randi_range(1, 1000))
	status.text = "Score submitted." if result.ok else "Couldn't submit it: %s" % result.error
