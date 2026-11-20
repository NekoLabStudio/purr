extends XRInterfaceExtension
## A stereo camera for the occlusion check in test.gd: two eyes EYES_APART apart, looking down -Z
## with a narrow field of view, so a box behind a wall's edge is in sight of one eye only.

const EYES_APART := 0.4
var ready := false
# XRCamera3D takes the eyes' projections only once a head tracker exists, as a headset's runtime adds.
var head: XRPositionalTracker


func _get_name() -> StringName:
	return &"Stereo test"


func _get_capabilities() -> int:
	return XR_STEREO


func _is_initialized() -> bool:
	return ready


func _initialize() -> bool:
	head = XRPositionalTracker.new()
	head.name = &"head"
	head.type = XRServer.TRACKER_CAMERA
	XRServer.add_tracker(head)
	ready = true
	return true


func _uninitialize() -> void:
	XRServer.remove_tracker(head)
	ready = false


func _get_render_target_size() -> Vector2:
	return Vector2(256, 256)


func _get_view_count() -> int:
	return 2


func _get_camera_transform() -> Transform3D:
	return Transform3D()


func _get_camera_offsets(_tracker_name: StringName) -> Array[Transform3D]:
	return [Transform3D(Basis(), Vector3(-0.5 * EYES_APART, 0, 0)), Transform3D(Basis(), Vector3(0.5 * EYES_APART, 0, 0))]


func _get_camera_projections(_tracker_name: StringName, aspect: float, z_near: float, z_far: float) -> Array[Projection]:
	var projection := Projection.create_perspective(30.0, aspect, z_near, z_far)
	return [projection, projection]
