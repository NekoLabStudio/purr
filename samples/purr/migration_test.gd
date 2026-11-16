# Three players in a crossplay lobby with host migration on; the host leaves and the game goes on
# with the player who joined first as host. Against a local copy of the online service (see
# test.gd), each from its own copy of this project (so each has an account of its own):
#   purr --headless --path <copy A> -s migration_test.gd -- host
#   purr --headless --path <copy B> -s migration_test.gd -- second
#   purr --headless --path <copy C> -s migration_test.gd -- third
# The host writes the lobby's code to /tmp/purr_lobby_code.txt for the others.
extends SceneTree

const CODE_FILE := "/tmp/purr_lobby_code.txt"

var role: String = OS.get_cmdline_user_args()[0] if not OS.get_cmdline_user_args().is_empty() else "host"
var done := false


func _initialize() -> void:
	ProjectSettings.set_setting("lobbies/host_migration", true)
	run.call_deferred()
	create_timer(60.0).timeout.connect(func(): finish(false, "timed out"))


func run() -> void:
	var signed_in: Dictionary = await Account.sign_in()
	if not signed_in.ok:
		finish(false, "not signed in: %s" % signed_in)
		return
	if role == "host":
		var created: Dictionary = await Lobbies.create()
		var file := FileAccess.open(CODE_FILE, FileAccess.WRITE)
		file.store_string(created.code)
		file.close()
		# Leave once both others are connected.
		var connected := 0
		while connected < 2:
			await get_multiplayer().peer_connected
			connected += 1
		await create_timer(1.0).timeout
		finish(true, "")
		return

	if role == "third":
		await create_timer(1.5).timeout  # Join after "second", who then takes over.
	while not FileAccess.file_exists(CODE_FILE):
		await create_timer(0.2).timeout
	var joined: Dictionary = await Lobbies.join(FileAccess.get_file_as_string(CODE_FILE))
	if not joined.ok:
		finish(false, "couldn't join: %s" % joined)
		return
	var is_host: bool = await Lobbies.host_changed
	if is_host != (role == "second"):
		finish(false, "the wrong player took over")
		return
	if role == "second":
		# The new host hears from the third player, over the new connection.
		var got: Array = await get_multiplayer().peer_packet
		finish(get_multiplayer().get_unique_id() == 1 and got[1].get_string_from_utf8() == "hello new host", "got %s" % [got])
	else:
		await get_multiplayer().connected_to_server
		get_multiplayer().send_bytes("hello new host".to_utf8_buffer(), 1)
		finish(true, "")


func finish(ok: bool, detail: String) -> void:
	if done:
		return
	done = true
	print("check: ", role, "" if ok else "  FAILED  " + detail)
	await create_timer(1.0).timeout
	if role == "host":
		DirAccess.remove_absolute(CODE_FILE)
	Lobbies.leave()
	quit(0 if ok else 1)
