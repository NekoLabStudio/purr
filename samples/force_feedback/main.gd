extends VBoxContainer
## Finds a force feedback wheel and plays what the sliders say. The wheel's axes and
## buttons still come through Input as a normal joypad; this only adds the forces.

var wheel := SteeringWheel.new()
var status := Label.new()


func _ready() -> void:
	add_child(status)
	var found := wheel.find_wheel()
	if found.is_empty():
		status.text = "No force feedback wheel found."
		return
	if not wheel.open():
		status.text = "Found %s, but couldn't open it." % found.name
		return
	status.text = "%s (%s driver%s)" % [found.name, found.family, ", not yet confirmed on this wheel" if found.new else ""]
	_add_slider("Force", -1.0, 1.0, func(force: float) -> void: wheel.force = force)
	_add_slider("Damper", 0.0, 1.0, func(damper: float) -> void: wheel.damper = damper)
	_add_slider("Vibration", 0.0, 1.0, func(amplitude: float) -> void: wheel.set_vibration(amplitude, 30.0))


func _add_slider(label: String, from: float, to: float, apply: Callable) -> void:
	var title := Label.new()
	title.text = label
	add_child(title)
	var slider := HSlider.new()
	slider.min_value = from
	slider.max_value = to
	slider.step = 0.01
	slider.value = 0.0
	slider.custom_minimum_size.x = 320
	slider.value_changed.connect(apply)
	add_child(slider)


func _exit_tree() -> void:
	wheel.close()
