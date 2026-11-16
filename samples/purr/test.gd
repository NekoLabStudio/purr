# Tries the Purr API against a local copy of the online service. Start it first, with the "log"
# email driver (EMAIL_DRIVER=log in online/.dev.vars) and its output in a file:
#   cd online && npx wrangler d1 migrations apply purr-online --local && npx wrangler dev > /tmp/purr-online.log
# then run, with that file (where the sign-in codes appear):
#   PURR_EMAIL_LOG=/tmp/purr-online.log purr --headless --path samples/purr -s test.gd
# It exits non-zero if a check fails. The service sends one code a minute to an address, so a run
# takes a little over a minute (a second device signs in with the player's email), and runs need a
# minute between them.
extends SceneTree

var failures := 0


func check(what: String, ok: bool, detail = null) -> void:
	print("check: ", what, "" if ok else "  FAILED  " + str(detail))
	if not ok:
		failures += 1


func remove_folder(path: String) -> void:
	for folder in DirAccess.get_directories_at(path):
		remove_folder(path.path_join(folder))
	for file in DirAccess.get_files_at(path):
		DirAccess.remove_absolute(path.path_join(file))
	DirAccess.remove_absolute(path)


func new_email(name: String) -> String:
	return "%s.%d@example.com" % [name, Time.get_ticks_usec()]


# The code last emailed to the address, read from the service's output.
func email_code(email: String) -> String:
	var pattern := RegEx.create_from_string("\\[email\\] to " + email.replace(".", "\\.") + ":[^\\n]*\\n[^\\n]* is (\\d{6})\\.")
	for attempt in 50:
		var matches := pattern.search_all(FileAccess.get_file_as_string(OS.get_environment("PURR_EMAIL_LOG")))
		if not matches.is_empty():
			return matches[-1].get_string(1)
		await create_timer(0.1).timeout
	return ""


# The service's HTTP API, as another device would use it: [ok, status, headers, body bytes].
func service(method: HTTPClient.Method, path: String, token := "", body := PackedByteArray(), headers := PackedStringArray()) -> Array:
	var request := HTTPRequest.new()
	root.add_child(request)
	var all_headers := PackedStringArray(["X-Purr-Game: " + str(ProjectSettings.get_setting("online/game"))]) + headers
	if token != "":
		all_headers.append("Authorization: Bearer " + token)
	request.request_raw(str(ProjectSettings.get_setting("online/url")) + path, all_headers, method, body)
	var answer: Array = await request.request_completed
	request.queue_free()
	var named := {}
	for line: String in answer[2]:
		named[line.get_slice(":", 0).strip_edges().to_lower()] = line.get_slice(":", 1).strip_edges()
	return [answer[1] >= 200 and answer[1] < 300, answer[1], named, answer[3]]


func json_body(fields: Dictionary) -> PackedByteArray:
	return JSON.stringify(fields).to_utf8_buffer()


# Signs another device in to the account with this email: its session token.
func other_device(email: String) -> String:
	var game := str(ProjectSettings.get_setting("online/game"))
	var started: Array = await service(HTTPClient.METHOD_POST, "/auth/email/start", "", json_body({"email": email, "game": game}), ["Content-Type: application/json"])
	var challenge: String = JSON.parse_string(started[3].get_string_from_utf8()).get("challenge", "")
	var verified: Array = await service(HTTPClient.METHOD_POST, "/auth/email/verify", "", json_body({"email": email, "challenge": challenge, "code": await email_code(email)}), ["Content-Type: application/json"])
	return JSON.parse_string(verified[3].get_string_from_utf8()).get("token", "") if verified[0] else ""


func read_remote(token: String, slot: String) -> Array:
	return await service(HTTPClient.METHOD_GET, "/saves/%s/%s" % [ProjectSettings.get_setting("online/game"), slot], token)


func write_remote(token: String, slot: String, data: Variant, replaces: int, summary: Dictionary) -> Array:
	return await service(HTTPClient.METHOD_PUT, "/saves/%s/%s" % [ProjectSettings.get_setting("online/game"), slot], token, var_to_bytes(data), ["Content-Type: application/octet-stream", "X-Save-Version: %d" % replaces, "X-Save-Summary: " + JSON.stringify(summary)])


func _initialize() -> void:
	if OS.get_environment("PURR_EMAIL_LOG").is_empty():
		print("Set PURR_EMAIL_LOG to the file wrangler dev writes to (see the top of this script).")
		quit(1)
		return
	run.call_deferred()
	create_timer(240.0).timeout.connect(func():
		print("FAILED: timed out (a code may have been refused; see the service's output)")
		quit(1))


func run() -> void:
	# A clean start: no device account or saves from an earlier run.
	DirAccess.remove_absolute("user://purr_credentials.cfg")
	remove_folder("user://purr")
	# Two sign-ins at once share one (each making its own device account would lose one).
	var first_sign_in := Account.sign_in()
	var overlapping := Account.sign_in()
	var signed_in: Dictionary = await first_sign_in
	check("signs in with a device account", signed_in.ok and signed_in.provider == Account.PROVIDER_DEVICE and signed_in.online, signed_in)
	check("a sign-in asked for meanwhile shares it", overlapping == first_sign_in)
	var account: String = LocalPlayers.player.account_id
	var again: Dictionary = await Account.sign_in()
	check("signing in again works, to the same account", again.ok and LocalPlayers.player.account_id == account, again)
	check("no email yet, so saves stay on this device", not Account.has_email() and Saves.get_backend() == PlayerSaves.BACKEND_LOCAL)
	check("email, guests and crossplay available", Account.can_link_email() and LocalPlayers.can_invite_guests() and Lobbies.has_crossplay())
	check("no achievements without a platform", not Achievements.is_available())
	check("no friends or invites without Steam or Game Center", not Friends.is_available() and not Lobbies.can_invite() and Friends.get_all().is_empty())
	check("no platform name, so the game asks for one", Account.needs_display_name())
	var named: Dictionary = await Account.set_display_name("  Tester  ")
	check("chosen name saved", named.ok and named.display_name == "Tester" and Account.player_name == "Tester" and not Account.needs_display_name(), named)
	check("too long a name refused", (await Account.set_display_name("x".repeat(25))).error == "bad_name")

	Saves.save("main", {"level": 1, "coins": 10}, {"level": 1})
	check("loads the save", Saves.load("main") == {"level": 1, "coins": 10} and Saves.has("main") and Saves.get_summary("main").level == 1)

	# Achievements without a platform or an email: kept with this device's saves.
	Achievements.unlock("first_steps")
	Achievements.set_progress("hundred_laps", 30, 100)
	Achievements.set_progress("hundred_laps", 10, 100)
	check("achievements kept before adding an email, progress never going back", Achievements.get_all() == {"first_steps": 1.0, "hundred_laps": 0.3} and Achievements.get_progress("hundred_laps") == 0.3, Achievements.get_all())
	check("the achievements' own slot isn't one of the game's", Saves.get_slots() == PackedStringArray(["main"]), Saves.get_slots())

	# Adding an email: this device's save goes up to the account.
	var email := new_email("player")
	var started: Dictionary = await Account.link_email(email)
	check("code sent", started.ok, started)
	var finished: Dictionary = await Account.confirm_email(await email_code(email))
	check("email added", finished.ok and not finished.merged and Account.has_email() and Saves.get_backend() == PlayerSaves.BACKEND_ONLINE, finished)
	check("this device's save goes up", (await Saves.synced) == "main")

	# Another device signs in with the same email (a minute later: one code a minute) and saves
	# version 2; this device, still at version 1, saves again.
	await create_timer(61.0).timeout
	var token := await other_device(email)
	check("another device signs in with the email", token != "")
	var remote: Array = await read_remote(token, "main")
	check("the service has the save", remote[0] and bytes_to_var(remote[3]) == {"level": 1, "coins": 10}, remote[1])
	var other: Array = await write_remote(token, "main", {"level": 5}, 1, {"level": 5})
	check("other device saved", other[0], other[1])
	Saves.save("main", {"level": 2}, {"level": 2})
	var conflict: Array = await Saves.conflict
	check("conflict offers both saves", conflict[0] == "main" and conflict[1].level == 2 and conflict[2].level == 5, conflict)
	Saves.resolve_conflict("main", true)
	await Saves.synced
	remote = await read_remote(token, "main")
	check("kept this device's save", bytes_to_var(remote[3]) == {"level": 2} and remote[2].get("x-save-version") == "3", remote[2])

	# Achievements never conflict: another device's list and this one's are merged.
	other = await write_remote(token, "purr_achievements", {"first_steps": 1.0, "hundred_laps": 0.5, "night_drive": 1.0}, 1, {})
	check("other device saved achievements", other[0], other[1])
	var conflicts := []
	var note_conflict := func(slot, _local, _remote): conflicts.append(slot)
	Saves.conflict.connect(note_conflict)
	Achievements.unlock("perfect_lap")
	var merged := {"first_steps": 1.0, "hundred_laps": 0.5, "night_drive": 1.0, "perfect_lap": 1.0}
	for attempt in 50:
		remote = await read_remote(token, "purr_achievements")
		if remote[0] and bytes_to_var(remote[3]) == merged:
			break
		await create_timer(0.1).timeout
	Saves.conflict.disconnect(note_conflict)
	check("achievements merged with the other device's, on the service", remote[0] and bytes_to_var(remote[3]) == merged, bytes_to_var(remote[3]) if remote[0] else remote[1])
	check("and here, with no conflict", Achievements.get_all() == merged and conflicts.is_empty(), [Achievements.get_all(), conflicts])

	# A plain guest becomes a local profile, then adds an email and keeps their progress.
	var guest: LocalPlayer = LocalPlayers.add_guest()
	check("plain guest", guest.kind == LocalPlayer.KIND_GUEST and LocalPlayers.players.size() == 2 and guest.saves == null)
	check("a plain guest needs a name", guest.needs_display_name() and guest.player_name == "Guest")
	check("plain guest named", (await guest.set_display_name("Player Two")).ok and guest.player_name == "Player Two" and not guest.needs_display_name())
	var remembered: Dictionary = await guest.remember("Kid")
	check("remembered as a local profile", remembered.ok and guest.kind == LocalPlayer.KIND_LOCAL_PROFILE and LocalPlayers.remembered.size() == 1, remembered)
	guest.saves.save("character", {"hat": "red"})
	check("profile's save", guest.saves.load("character") == {"hat": "red"})
	check("local profile renamed and kept", (await guest.set_display_name("Kiddo")).ok and LocalPlayers.remembered[0].player_name == "Kiddo")
	var kid_email := new_email("kid")
	await guest.link_email(kid_email)
	var kid: Dictionary = await guest.confirm_email(await email_code(kid_email))
	check("local profile adds an email", kid.ok and guest.kind == LocalPlayer.KIND_ONLINE_GUEST and guest.remembered and LocalPlayers.remembered.size() == 1, kid)
	await guest.saves.synced
	check("profile's save goes up to their account", guest.saves.load("character") == {"hat": "red"})

	# Bringing a profile: a guest types their email here, then their code.
	var ines := new_email("ines")
	check("guest code sent", (await LocalPlayers.invite_guest(ines)).ok)
	var brought: Dictionary = await LocalPlayers.confirm_guest(await email_code(ines), false, "Inês")
	check("guest arrives", brought.ok and brought.guest.player_name == "Inês" and brought.guest.kind == LocalPlayer.KIND_ONLINE_GUEST and not brought.guest.remembered, brought)
	check("a brought guest's name is theirs to change", (await brought.guest.set_display_name("Not Inês")).error == "own_device" and brought.guest.player_name == "Inês")
	brought.guest.saves.save("character", {"hat": "blue"})
	await brought.guest.saves.synced
	check("brought guest saves to their own account", brought.guest.saves.load("character") == {"hat": "blue"})
	brought.guest.set_achievement_progress("hundred_laps", 2, 4)
	check("a guest's achievements are their own", brought.guest.get_achievements() == {"hundred_laps": 0.5} and Achievements.get_all().hundred_laps == 0.5, brought.guest.get_achievements())

	guest.forget()
	check("forgotten", LocalPlayers.remembered.is_empty())

	var devices: Dictionary = await Account.get_devices()
	check("devices that remember the player listed", devices.ok and devices.devices is Array, devices)
	var deleted: Dictionary = await Account.delete_account()
	check("account deleted, and signed out here", deleted.ok and not Account.is_signed_in(), deleted)

	print("purr test: ", "all checks passed" if failures == 0 else "%d failed" % failures)
	quit(failures)
