extends Node3D
# Starts OpenXR the usual way. VR is off until the player turns it on (VR.enabled, from the next
# start) or the launch asks for it: run with -openxr to play in a headset. On a Mac, Purr's
# runtime then shows the game in the Steam Link app on the same network, or a Vision Pro. Run
# with -- --test to check without a headset: it prints the VR state and what the left controller
# reports (a headset, or a test playing the streamer's part, supplies it), and quits after a few
# seconds.

var frames := 0

func _ready():
	var xr := XRServer.find_interface("OpenXR")
	if xr and xr.is_initialized():
		get_viewport().use_xr = true
	var test := OS.get_cmdline_user_args().has("--test")
	print("check: VR available %s, can choose %s, enabled %s, running %s" % [VR.is_available(), VR.can_choose(), VR.enabled, VR.is_running()])
	if test:
		await get_tree().create_timer(float(OS.get_environment("VR_SAMPLE_SECONDS")) if OS.has_environment("VR_SAMPLE_SECONDS") else 6.0).timeout
		print("check: %d frames drawn" % frames)
		var left: XRController3D = $XROrigin3D/LeftHand
		print("check: left controller %s, trigger %.2f, at %s" % ["tracked" if left.get_has_tracking_data() else "not tracked", left.get_float("trigger"), left.position.snapped(Vector3(0.01, 0.01, 0.01))])
		get_tree().quit()

func _process(_delta):
	frames += 1
