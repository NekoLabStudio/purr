extends Control

# Each of the game's store items with what it costs, whether the player owns it, and a Buy button.

const ITEMS := ["winter_expansion", "soundtrack"]

@onready var list: VBoxContainer = $Margin/Rows/Items
@onready var status: Label = $Margin/Rows/Status


func _ready() -> void:
	Store.ownership_changed.connect(func(_id): _show())
	$Margin/Rows/Restore.pressed.connect(_restore)
	if not Store.is_available():
		status.text = "No store here: run it from Steam, on an Apple device or on Android, or from the editor."
	_show()


func _show() -> void:
	for child in list.get_children():
		child.queue_free()
	for id in ITEMS:
		var row := HBoxContainer.new()
		var label := Label.new()
		label.text = "%s  %s" % [id, Store.get_price(id)]
		label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(label)
		var buy := Button.new()
		buy.text = "Owned" if Store.owns(id) else "Buy"
		buy.disabled = Store.owns(id)
		buy.pressed.connect(_buy.bind(id))
		row.add_child(buy)
		list.add_child(row)


func _buy(id: String) -> void:
	var result: Dictionary = await Store.purchase(id)
	status.text = "Bought %s." % id if result.ok else "%s: %s" % [result.error, result.message]
	_show()


func _restore() -> void:
	var result: Dictionary = await Store.restore()
	status.text = "Purchases restored." if result.ok else result.message
	_show()
