extends VBoxContainer
## Steam starts with the project (Project Settings > Steam): this shows who's signed in,
## opens the overlay, and invites a friend to a lobby. Accepting the invite on another
## computer running this sample joins it.

var status := Label.new()


func _ready() -> void:
	add_child(status)
	var started: Dictionary = Steam.get_steam_init_result()
	if started.get("status") != Steam.STEAM_API_INIT_RESULT_OK:
		status.text = "Steam didn't start: %s" % started.get("verbal", "it isn't running.")
		return
	status.text = "Signed in as %s." % Steam.getPersonaName()

	var overlay := Button.new()
	overlay.text = "Open the Steam overlay"
	overlay.pressed.connect(Steam.activateGameOverlay.bind("Friends"))
	add_child(overlay)

	var invite := Button.new()
	invite.text = "Invite a friend"
	invite.pressed.connect(Steam.createLobby.bind(Steam.LOBBY_TYPE_FRIENDS_ONLY, 2))
	add_child(invite)

	Steam.lobby_created.connect(_on_lobby_created)
	Steam.join_requested.connect(_on_join_requested)
	Steam.lobby_joined.connect(_on_lobby_joined)


func _on_lobby_created(result: int, lobby_id: int) -> void:
	if result != Steam.RESULT_OK:
		status.text = "Couldn't create a lobby (result %d)." % result
		return
	Steam.activateGameOverlayInviteDialog(lobby_id)


func _on_join_requested(lobby_id: int, _friend_id: int) -> void:
	Steam.joinLobby(lobby_id)


func _on_lobby_joined(lobby_id: int, _permissions: int, _locked: bool, _response: int) -> void:
	status.text = "In a lobby with %d players." % Steam.getNumLobbyMembers(lobby_id)
