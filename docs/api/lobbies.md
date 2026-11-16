# Lobbies

```gdscript
var created = await Lobbies.create()
show_code(created.code)
```

`Lobbies.create()` makes a lobby the player hosts and sets the scene tree's multiplayer peer, so the game plays through Godot's [high-level multiplayer](https://docs.godotengine.org/en/latest/tutorials/networking/high_level_multiplayer.html) as usual. The host is peer 1. Other players join with the code:

```gdscript
var joined = await Lobbies.join(code)
```

`Lobbies.leave()` leaves and clears the multiplayer peer. `Lobbies.joined` fires whenever the player joins a lobby, including from an invite.

## Crossplay lobbies

With the online services set up and the player signed in to them, lobbies are crossplay: players on any platform join with the code and play peer to peer over [WebRTC](../modules/webrtc.md). The service only introduces the players; gameplay never goes through it. When two players can't connect directly, the connection goes through a TURN relay, if the service has one set up (see [the online services](../online/index.md#turn)).

Playing peer to peer shows each player's IP address to the players they connect to. Turn on `lobbies/relay_only` to connect only through TURN, so nobody sees anyone's address.

## Removing, reporting and blocking players

In a crossplay lobby, the host can remove a player, who then can't come back to that lobby:

```gdscript
Lobbies.kick(peer_id)
```

The removed player's game gets `Lobbies.kicked`. The game decides whether to offer this to hosts, for example in the lobby's menu.

Any player can report or block someone they're playing with:

```gdscript
Lobbies.report(peer_id, Lobbies.REPORT_CHEATING, "Drove through walls on lap 2")
Lobbies.block(peer_id)
```

The reason is `REPORT_CHEATING`, `REPORT_ABUSE`, `REPORT_NAME` or `REPORT_OTHER`, and the details are up to 500 characters in the player's own words. Reports go to whoever runs the online services (see [Moderation](../online/moderation.md)). A blocked player is never matched with the player who blocked them again, and can't join lobbies they host.

## When the host leaves

By default, a lobby's game ends for everyone when the host leaves: their connections to the host close, and Godot's multiplayer emits `server_disconnected`.

Turn on `lobbies/host_migration` to keep the game going instead. The player who has been in the lobby longest becomes the host, as peer 1, and everyone else reconnects to them. `Lobbies` emits `host_changed`, with `is_host` true on the new host's device. Nodes owned by peer 1 belong to the new host from then on, so a game whose state every player already has (synchronised with `MultiplayerSynchronizer`, for example) carries on. Godot's multiplayer sees the old host disconnect and the new one connect, so a game that resets on `server_disconnected` should check for `host_changed` first.

## Steam lobbies

Without the online services, on Steam, lobbies are Steam's, for Steam players only. The code is the Steam lobby's ID and the game plays over Steam's networking. A Steam lobby holds `lobbies/max_players` players (4 unless set, up to 8); a crossplay lobby holds up to 8.

## Friends and invites

`Lobbies.invite_friends()` opens the platform's invite screen for the current lobby: Steam's, or Game Center's for a crossplay lobby on Apple platforms. A friend who accepts joins the lobby, starting the game first if needed, and `Lobbies.joined` fires on their side. Without Steam or Game Center, players share the lobby's code. `Lobbies.can_invite()` says whether invites work here.

On Steam, the game can show friends itself:

```gdscript
for friend in Friends.get_all():
    if friend.lobby:
        add_row(friend.name, "Join" if not friend.invited else "Join (invited)")

Friends.changed.connect(refresh_friends)
```

`Friends.get_all()` returns each Steam friend as a dictionary with `id`, `name`, `playing` (true when they're playing this game), `lobby` (the code of the lobby they're in, or empty) and `invited` (true when they invited this player to it). Joining a friend's lobby is `Lobbies.join(friend.lobby)`. `Friends.invite(friend.id)` invites one friend to the current lobby; Steam tells them, and the game sees `invited` in their list. `Friends.changed` fires when a friend's status or lobby changes. Elsewhere `Friends.get_all()` is empty and `Friends.is_available()` is false.

The lobby a player is in shows in their Steam rich presence, so friends can also join it from Steam's own friends list with "Join Game". Ranked lobbies from matchmaking aren't shown.

### Game Center parties

On Apple platforms, a crossplay lobby can also be a Game Center party, which friends see and join in Apple's Games app. Set up a game activity with party codes in App Store Connect and put its ID in `game_center/party_activity`. Each player in the lobby then starts that activity with a party code made from the lobby's code, and a player who joins the party from the Games app joins the lobby. Parties need iOS, macOS, tvOS or visionOS 26 or later; on earlier versions the setting does nothing.
