# Connects two WebRTC peers inside one process and sends messages both ways over a data channel.
# Run: purr --headless --path samples/webrtc -s test.gd (it exits non-zero if a check fails).
extends SceneTree

var a := WebRTCPeerConnection.new()
var b := WebRTCPeerConnection.new()
var channel_a: WebRTCDataChannel
var channel_b: WebRTCDataChannel
var got_a := ""
var got_b := ""
var sent := false
var done := false


func _initialize() -> void:
	# Signalling goes straight across instead of through a server.
	a.session_description_created.connect(func(type, sdp): b.set_remote_description(type, sdp))
	b.session_description_created.connect(func(type, sdp): a.set_remote_description(type, sdp))
	a.ice_candidate_created.connect(func(media, index, name): b.add_ice_candidate(media, index, name))
	b.ice_candidate_created.connect(func(media, index, name): a.add_ice_candidate(media, index, name))
	b.data_channel_received.connect(func(channel): channel_b = channel)
	channel_a = a.create_data_channel("purr")
	if channel_a == null or a.create_offer() != OK:
		finish(false, "couldn't start the connection")
		return
	create_timer(10.0).timeout.connect(func(): finish(false, "timed out"))


func _process(_delta: float) -> bool:
	a.poll()
	b.poll()
	if done:
		return false
	if not sent and channel_a.get_ready_state() == WebRTCDataChannel.STATE_OPEN:
		sent = true
		channel_a.put_packet("hello from a".to_utf8_buffer())
	if channel_b != null and channel_b.get_available_packet_count() > 0:
		got_b = channel_b.get_packet().get_string_from_utf8()
		channel_b.put_packet("hello from b".to_utf8_buffer())
	if channel_a.get_available_packet_count() > 0:
		got_a = channel_a.get_packet().get_string_from_utf8()
		finish(got_b == "hello from a" and got_a == "hello from b", "got %s / %s" % [got_b, got_a])
	return false


func finish(ok: bool, detail: String) -> void:
	if done:
		return
	done = true
	print("check: data channel round trip", "" if ok else "  FAILED  " + detail)
	a.close()
	b.close()
	quit(0 if ok else 1)
