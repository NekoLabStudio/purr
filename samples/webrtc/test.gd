# Connects two WebRTC peers inside one process and sends messages both ways over data channels.
# Then moves the connection onto new ICE servers (as when TURN credentials are renewed) while
# both sides keep sending, twice (once from one side, once from both at the same moment), and
# checks that nothing was lost, repeated or reordered on the reliable channels.
# Run: purr --headless --path samples/webrtc -s test.gd (it exits non-zero if a check fails).
extends SceneTree

const PER_FRAME := 20

var a := WebRTCPeerConnection.new()
var b := WebRTCPeerConnection.new()
var failures := 0

# Channels by name, on each side: "purr" (made by a, received by b), and "reliable" and
# "unreliable", negotiated on both, as WebRTCMultiplayerPeer makes them.
var channels_a := {}
var channels_b := {}
var next_sent := {} # "<side>:<channel>" -> the next number to send
var received := {} # "<side>:<channel>" -> the numbers received, in order


func check(what: String, ok: bool, detail: Variant = null) -> void:
	print("check: ", what, "" if ok else "  FAILED  %s" % [detail])
	if not ok:
		failures += 1


func _initialize() -> void:
	# Signalling goes straight across instead of through a server.
	a.session_description_created.connect(func(type, sdp): b.set_remote_description(type, sdp))
	b.session_description_created.connect(func(type, sdp): a.set_remote_description(type, sdp))
	a.ice_candidate_created.connect(func(media, index, name): b.add_ice_candidate(media, index, name))
	b.ice_candidate_created.connect(func(media, index, name): a.add_ice_candidate(media, index, name))
	b.data_channel_received.connect(func(channel): channels_b["purr"] = channel)
	channels_a["purr"] = a.create_data_channel("purr")
	for side in [[a, channels_a], [b, channels_b]]:
		side[1]["reliable"] = side[0].create_data_channel("reliable", { "negotiated": true, "id": 1 })
		side[1]["unreliable"] = side[0].create_data_channel("unreliable", { "negotiated": true, "id": 3, "ordered": false, "maxRetransmits": 0 })
	_run.call_deferred()


func _process(_delta: float) -> bool:
	a.poll()
	b.poll()
	for side in [["a", channels_a], ["b", channels_b]]:
		for name in side[1]:
			var channel: WebRTCDataChannel = side[1][name]
			while channel.get_available_packet_count() > 0:
				var key: String = "%s:%s" % [side[0], name]
				if not received.has(key):
					received[key] = []
				received[key].append(channel.get_packet().decode_u32(0))
	return false


func wait_until(condition: Callable, seconds: float) -> bool:
	var started := Time.get_ticks_msec()
	while not condition.call():
		if Time.get_ticks_msec() - started > seconds * 1000.0:
			return false
		await process_frame
	return true


func open() -> bool:
	for channels in [channels_a, channels_b]:
		if not channels.has("purr"):
			return false
		for channel in channels.values():
			if channel.get_ready_state() != WebRTCDataChannel.STATE_OPEN:
				return false
	return true


# Each side sends PER_FRAME numbered packets a frame on every channel.
func send_frame() -> void:
	for side in [["a", channels_a], ["b", channels_b]]:
		for name in side[1]:
			var key: String = "%s:%s" % [side[0], name]
			for i in PER_FRAME:
				var packet := PackedByteArray()
				packet.resize(4)
				packet.encode_u32(0, next_sent.get(key, 0))
				next_sent[key] = next_sent.get(key, 0) + 1
				side[1][name].put_packet(packet)


func send_for(seconds: float) -> void:
	var started := Time.get_ticks_msec()
	while Time.get_ticks_msec() - started < seconds * 1000.0:
		send_frame()
		await process_frame


# What one side sent on a reliable channel, as the other received it: every number, once, in order.
func all_in_order(from: String, to: String, name: String) -> bool:
	var got: Array = received.get("%s:%s" % [to, name], [])
	if got.size() != next_sent.get("%s:%s" % [from, name], 0):
		return false
	for i in got.size():
		if got[i] != i:
			return false
	return true


# For a failure: how many packets each side received on a channel, of how many the other sent.
func arrived(name: String) -> String:
	return "a to b %d of %d, b to a %d of %d" % [received.get("b:" + name, []).size(), next_sent.get("a:" + name, 0), received.get("a:" + name, []).size(), next_sent.get("b:" + name, 0)]


func moves_kept_everything(what: String) -> void:
	# The last packets arrive, on every reliable channel both ways.
	await wait_until(func(): return all_in_order("a", "b", "reliable") and all_in_order("b", "a", "reliable") and all_in_order("a", "b", "purr") and all_in_order("b", "a", "purr"), 10.0)
	check(what + ": reliable packets arrive once each, in order, both ways", all_in_order("a", "b", "reliable") and all_in_order("b", "a", "reliable"), arrived("reliable"))
	check(what + ": on a channel the other side opened too", all_in_order("a", "b", "purr") and all_in_order("b", "a", "purr"), arrived("purr"))
	check(what + ": the connection stayed connected", a.get_connection_state() == WebRTCPeerConnection.STATE_CONNECTED and b.get_connection_state() == WebRTCPeerConnection.STATE_CONNECTED)
	check(what + ": the channels stayed open", open())


func _run() -> void:
	check("a native connection, which can move", a.has_method("renew_ice_servers"))
	if a.create_offer() != OK:
		check("starts a connection", false)
		quit(1)
		return
	check("connects and opens its channels", await wait_until(open, 10.0))
	check("not through a relay here", not a.uses_relay())

	await send_for(0.5)
	check("moving starts", a.renew_ice_servers([]) == OK and a.is_renewing())
	await send_for(0.5)
	check("and finishes on both sides", await wait_until(func(): return not a.is_renewing() and not b.is_renewing(), 15.0))
	var unreliable_before: int = received.get("b:unreliable", []).size()
	await send_for(0.5)
	check("unreliable packets still arrive after the move", received.get("b:unreliable", []).size() > unreliable_before)
	await moves_kept_everything("one side moving")

	# Both sides renew at the same moment: one move goes ahead.
	await send_for(0.3)
	a.renew_ice_servers([])
	b.renew_ice_servers([])
	await send_for(0.5)
	check("two moves at once settle into one", await wait_until(func(): return not a.is_renewing() and not b.is_renewing(), 15.0))
	await send_for(0.3)
	await moves_kept_everything("both sides moving at once")

	a.close()
	b.close()
	print("webrtc test: %s" % ("all checks passed" if failures == 0 else "%d failed" % failures))
	quit(failures)
