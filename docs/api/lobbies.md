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

With the online services set up and the player signed in to them, lobbies are crossplay: players on any platform join with the code and play peer to peer over [WebRTC](../modules/webrtc.md). The service only introduces the players; gameplay never goes through it. When two players can't connect directly, the connection goes through a TURN relay, if the service has one set up (see [the online services](https://oss.nekolab.studio/trill/turn/)).

Playing peer to peer shows each player's IP address to the players they connect to. Turn on `lobbies/relay_only` to connect only through TURN, so nobody sees anyone's address. Only players who get TURN can then connect: by default, those signed in through Steam, Game Center or Play Games (see [TURN](https://oss.nekolab.studio/trill/turn/)).

## Removing, reporting and blocking players

The host can remove a player, who then can't come back to that lobby:

```gdscript
Lobbies.kick(peer_id)
```

The removed player's game gets `Lobbies.kicked`. The game decides whether to offer this to hosts, for example in the lobby's menu.

Any player can report or block someone they're playing with:

```gdscript
Lobbies.report(peer_id, Lobbies.REPORT_CHEATING, "Drove through walls on lap 2")
Lobbies.block(peer_id)
```

The reason is `REPORT_CHEATING`, `REPORT_ABUSE`, `REPORT_NAME` or `REPORT_OTHER`, and the details are up to 500 characters in the player's own words. In a crossplay lobby, reports go to whoever runs the online services (see [Moderation](https://oss.nekolab.studio/trill/moderation/)). In a Steam lobby, `report()` opens the player's Steam profile in the overlay, where Steam's own report button is; Game Center has nothing a game can report to, so there it does nothing. A blocked player is never matched with the player who blocked them again, and can't join lobbies they host.

## When the host leaves

By default, a lobby's game ends for everyone when the host leaves: their connections to the host close, and Godot's multiplayer emits `server_disconnected`.

Turn on `lobbies/host_migration` to keep the game going instead. The player who has been in the lobby longest becomes the host, as peer 1, and everyone else reconnects to them. `Lobbies` emits `host_changed`, with `is_host` true on the new host's device. Nodes owned by peer 1 belong to the new host from then on, so a game whose state every player already has (synchronised with `MultiplayerSynchronizer`, for example) carries on. Godot's multiplayer sees the old host disconnect and the new one connect, so a game that resets on `server_disconnected` should check for `host_changed` first.

## Without the online services

Without the online services (or when the player isn't signed in to them), lobbies are the platform's own, for players on that platform: Steam lobbies, or Game Center matches on Apple platforms. Everything above works the same: removing, blocking, host migration, [checks and flags](checks.md) and [ranked play](ranked.md). With no service to decide them, every player's game decides them together from the same messages, by the same rules, so no player can decide for the others.

### Steam

The code is the Steam lobby's ID, and the game plays over Steam's networking. A lobby made with a code is friends-only: friends see it, and anyone with the code can join. It holds `lobbies/max_players` players (4 unless set, up to 8; a crossplay lobby holds up to 8). With host migration, Steam picks the new host.

### Game Center

The game plays over a Game Center match, through Apple's servers. The player who creates the lobby hosts it. Friends join through invites (`Lobbies.invite_friends()` opens Apple's invite screen). To give lobbies a code others can join with, set up a game activity with party codes in App Store Connect and put its ID in `game_center/party_activity`: the code is then a Game Center party code, and friends also see the party in Apple's Games app. Codes need iOS, macOS, tvOS or visionOS 26 or later; without them, `Lobbies.create()` gives an empty code. With host migration, the remaining player with the lowest Game Center ID takes over.

## Friends and invites

`Lobbies.invite_friends()` opens the platform's invite screen for the current lobby: Steam's, or Game Center's on Apple platforms. A friend who accepts joins the lobby, starting the game first if needed, and `Lobbies.joined` fires on their side. Without Steam or Game Center, players share the lobby's code. `Lobbies.can_invite()` says whether invites work here.

The game can also show friends itself:

```gdscript
for friend in Friends.get_all():
    if friend.lobby:
        add_row(friend.name, "Join" if not friend.invited else "Join (invited)")

Friends.changed.connect(refresh_friends)
```

`Friends.get_all()` returns each friend as a dictionary with `id`, `name`, `playing` (true when they're playing this game), `lobby` (the code of the lobby they're in, or empty) and `invited` (true when they invited this player to it). Joining a friend's lobby is `Lobbies.join(friend.lobby)`. `Friends.invite(friend.id)` invites one friend to the current lobby; the platform tells them, and on Steam the game sees `invited` in their list. `Friends.changed` fires when a friend's status or lobby changes.

On Game Center and Play Games, the first `Friends.get_all()` asks the platform for the player's friends, which asks the player whether to share them; the list is empty until they answer, and `Friends.changed` fires when it arrives. Neither platform says what friends are playing, so `playing`, `lobby` and `invited` stay empty there. On Game Center, `Friends.invite()` works too; Play Games has no invites. Elsewhere `Friends.get_all()` is empty and `Friends.is_available()` is false.

The lobby a player is in shows in their Steam rich presence, so friends can also join it from Steam's own friends list with "Join Game". Ranked lobbies from matchmaking aren't shown.

### Game Center parties

On Apple platforms, a crossplay lobby can also be a Game Center party, which friends see and join in Apple's Games app. Set up a game activity with party codes in App Store Connect and put its ID in `game_center/party_activity`. Each player in the lobby then starts that activity with a party code made from the lobby's code, and a player who joins the party from the Games app joins the lobby. Parties need iOS, macOS, tvOS or visionOS 26 or later; on earlier versions the setting does nothing.
