# Two players in a crossplay lobby, against a local copy of the online service (see test.gd).
# Run the host, then the guest, in two terminals:
#   purr --headless --path samples/purr -s lobby_test.gd -- host
#   purr --headless --path samples/purr -s lobby_test.gd -- guest
# Add "kick" to both for the host to remove the guest afterwards (run the guest from a copy of
# this project then, with another config/name, so the two have different accounts).
# The host writes the lobby's code to /tmp/purr_lobby_test_code.txt for the guest. Each exits non-zero if
# its check fails.
extends SceneTree

const CODE_FILE := "/tmp/purr_lobby_test_code.txt"

var host := OS.get_cmdline_user_args().has("host")
# With "kick", the host then removes the guest, who checks they're told.
var kick := OS.get_cmdline_user_args().has("kick")
var done := false


func _initialize() -> void:
	run.call_deferred()
	create_timer(30.0).timeout.connect(func(): finish(false, "timed out"))


func run() -> void:
	var signed_in: Dictionary = await Account.sign_in()
	if not signed_in.ok or not Lobbies.has_crossplay():
		finish(false, "not signed in to the online service: %s" % signed_in)
		return
	get_multiplayer().peer_packet.connect(packet_received)
	if host:
		get_multiplayer().peer_connected.connect(func(id): get_multiplayer().send_bytes("hello guest".to_utf8_buffer(), id))
		var created: Dictionary = await Lobbies.create()
		if not created.ok:
			finish(false, "couldn't create: %s" % created)
			return
		var file := FileAccess.open(CODE_FILE, FileAccess.WRITE)
		file.store_string(created.code)
		file.close()
		print("lobby ", created.code, ", waiting for the guest")
	else:
		while not FileAccess.file_exists(CODE_FILE):
			await create_timer(0.2).timeout
		var code := FileAccess.get_file_as_string(CODE_FILE)
		DirAccess.remove_absolute(CODE_FILE)
		var joined: Dictionary = await Lobbies.join(code)
		if not joined.ok:
			finish(false, "couldn't join: %s" % joined)
			return


func packet_received(id: int, packet: PackedByteArray) -> void:
	var text := packet.get_string_from_utf8()
	if host:
		if kick:
			Lobbies.kick(id)
		finish(id != 1 and text == "hello host", "got %s from %d" % [text, id])
	else:
		get_multiplayer().send_bytes("hello host".to_utf8_buffer(), 1)
		if kick:
			await Lobbies.kicked
		finish(id == 1 and text == "hello guest", "got %s from %d" % [text, id])


func finish(ok: bool, detail: String) -> void:
	if done:
		return
	done = true
	print("check: ", "host" if host else "guest", " talks over the lobby", "" if ok else "  FAILED  " + detail)
	# Let the last packet go out before leaving.
	await create_timer(0.5).timeout
	Lobbies.leave()
	quit(0 if ok else 1)
