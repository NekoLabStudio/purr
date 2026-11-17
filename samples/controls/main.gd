# Press anything on any controller, keyboard or mouse: it shows what Purr makes of it, its name
# and its prompt (in both styles), and lists what's plugged in. For checking a controller's
# family and a steering wheel's button names by hand.
extends Control

var _devices: Label
var _pressed: Label
var _standard: TextureRect
var _pixel: TextureRect


func _ready() -> void:
	var column := VBoxContainer.new()
	column.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	column.add_theme_constant_override("separation", 16)
	add_child(column)
	_devices = Label.new()
	column.add_child(_devices)
	_pressed = Label.new()
	_pressed.text = "Press anything."
	column.add_child(_pressed)
	var images := HBoxContainer.new()
	column.add_child(images)
	_standard = TextureRect.new()
	_pixel = TextureRect.new()
	_pixel.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	_pixel.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_pixel.custom_minimum_size = Vector2(64, 64)
	_pixel.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT
	images.add_child(_standard)
	images.add_child(_pixel)
	InputBindings.devices_changed.connect(_list_devices)
	_list_devices()


func _list_devices() -> void:
	var lines := PackedStringArray(["Plugged in:"])
	for device in InputBindings.get_player_devices(0):
		lines.append("  %d: %s, %s%s" % [device, InputBindings.get_device_name(device), ClassDB.class_get_enum_constants("InputBindings", "DeviceFamily")[InputBindings.get_device_family(device)],
			"" if device == InputBindings.KEYBOARD else " (%s)" % Input.get_joy_info(device)])
	_devices.text = "\n".join(lines)


func _input(event: InputEvent) -> void:
	var binding := {}
	if event is InputEventKey and event.pressed and not event.echo:
		binding = {"key": OS.get_keycode_string(event.physical_keycode)}
	elif event is InputEventMouseButton and event.pressed:
		binding = {"mouse": ["", "left", "right", "middle", "wheel_up", "wheel_down"][mini(event.button_index, 5)]}
	elif event is InputEventJoypadButton and event.pressed:
		var hat: String = {124: "up", 125: "right", 126: "down", 127: "left"}.get(event.button_index, "")
		binding = {"device": Input.get_joy_name(event.device), "guid": Input.get_joy_guid(event.device)}
		if hat != "":
			binding["hat"] = hat
		else:
			binding["button"] = event.button_index
	elif event is InputEventJoypadMotion and absf(event.axis_value) > 0.6:
		binding = {"device": Input.get_joy_name(event.device), "guid": Input.get_joy_guid(event.device), "axis": event.axis, "to": signf(event.axis_value)}
	if binding.is_empty():
		return
	_pressed.text = "%s\n%s" % [InputBindings.describe_binding(binding), binding]
	ProjectSettings.set_setting("input_devices/prompts/style", "standard")
	_standard.texture = InputBindings.get_binding_prompt(binding)
	ProjectSettings.set_setting("input_devices/prompts/style", "pixel")
	_pixel.texture = InputBindings.get_binding_prompt(binding)
	ProjectSettings.set_setting("input_devices/prompts/style", "standard")
