# Steering wheels

A steering wheel's axes and buttons arrive through `Input` like any joypad. `SteeringWheel` adds force feedback, on macOS and Windows:

```gdscript
var wheel := SteeringWheel.new()


func _ready() -> void:
	if wheel.find_wheel().is_empty():
		return
	wheel.open()


func _physics_process(_delta: float) -> void:
	wheel.force = steering_force
	wheel.damper = 0.3
```

Set the forces as often as you like. A thread of its own sends them to the wheel as fast as the wheel accepts, so the feel doesn't depend on the frame rate and the game never waits for the wheel. Besides a steady force there's engine vibration, road texture, damping, friction, a spring towards a centre (`set_spring`, pushing back whichever way the wheel's axis runs), a steering spring and a column lock for when the car is off. `rev_lights` lights the rev lights on wheels that have them (the Logitech G27, G29 and G923 for PlayStation).

On macOS it drives Logitech, Thrustmaster, Fanatec and Simagic wheels and generic HID force feedback wheels; on Windows, any DirectInput force feedback wheel. Elsewhere `find_wheel()` returns an empty dictionary and nothing happens.

On macOS, turn on `input_devices/steering_wheels` in Project Settings for the wheel's axes and buttons to reach `Input`. Otherwise Apple's Game Controller framework takes the controllers and doesn't pass wheels on. With it on, every controller is read through SDL's own drivers instead, which support most popular controllers, so it's only worth turning on in games played with a wheel.

The force feedback doesn't need Godot: `WheelSession` (`modules/force_feedback/wheel_session.h`) is the same thread and wheels as plain C++, which other programs can build into their own, without exceptions or RTTI. Everything in the module except `steering_wheel.*`, `steering_wheel_hats.*` and `register_types.*` builds outside Godot; on macOS it opens wheels without taking them over, so other programs can still read their input.

`SteeringWheelHats` reads hat switches and the extra buttons some wheels have that don't come through as joypad buttons.

The class reference has every method: [SteeringWheel](../reference/SteeringWheel.md) and [SteeringWheelHats](../reference/SteeringWheelHats.md). `samples/force_feedback` tries a wheel with sliders.
