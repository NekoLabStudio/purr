# Ranked play

Games can match players of a similar skill and keep a rating for each player in each ranked mode, with the online services or, without them, on Steam and Game Center (see [Without the online services](#without-the-online-services)). A mode is any name the game chooses, such as `"duel"` or `"race-4"`.

```gdscript
var result = await Lobbies.matchmake("duel", 2)
if result.ok:
	start_match()
```

`matchmake()` waits until it finds the number of players asked for (2 to 8) with ratings close to the player's, then joins them in a new crossplay lobby, the same as [`Lobbies.join()`](lobbies.md). Whoever waited longest hosts it. Players who blocked each other are never matched. `Lobbies.cancel_matchmaking()` stops waiting.

Matchmaking prefers players near each other, since the farther apart they are, the higher the latency between them. Players can't measure their latency to each other before they're connected, so the service uses distance: Cloudflare's estimate of each player's city, from their connection. It starts with players within about 1,500 km and the same rating band, and widens both the distance and the rating range every five seconds a player waits, until anyone will do. The location is only kept while the player waits.

When the match is over, every player's game reports who finished where, first to last, by peer ID:

```gdscript
Lobbies.report_result([winner_peer_id, 1, last_peer_id])
```

Every player in the lobby must be in the list. The service changes ratings once enough players have reported the same result: more than half of them by default, so in a match of two both must agree, and nobody can change ratings alone. `result_recorded` fires when it has. Ratings are Elo: everyone starts at 1000, and beating a higher-rated player moves ratings more than beating a lower-rated one. A lobby's result counts once, and lobbies made with a code are never ranked.

The service's operator can change how many must agree, for each game, with `ranked_agreement` in its `GAMES` settings: `"majority"`, `"all"` or `"host"` (the host's report alone).

```gdscript
var rating = await Lobbies.get_rating("duel")
print(rating.data.rating, " after ", rating.data.matches, " matches")
```

## Without the online services

On Steam and Game Center without the online services, the same calls match players on the platform alone:

- On Steam, `matchmake()` looks for an open lobby of the same mode and size with ratings close to the player's, and opens one to wait in when there's none. It widens the rating range and the distance every five seconds the player waits, until anyone anywhere will do. The lobby's owner hosts.
- On Game Center, Game Center's matchmaking finds players of the same mode and size in a similar rating band, and widens the band every five seconds. The player with the lowest Game Center ID hosts.

Results are agreed by the players' games instead of the service: once more than half of them report the same result, each player's game changes its own rating, by the same Elo as the service, and `result_recorded` fires. Each player's rating is kept in their saves (and synced with them through Steam Cloud or iCloud), so `get_rating()` answers at once, even offline. Ratings don't carry between Steam, Game Center and the online services.

A rating kept on the player's device is only as safe as the device: a player who edits their saves can change their own rating. That suits friendly ranked play; for ratings players can't touch, use the online services.

## Limits

[Checks](checks.md) can catch a player whose game reports something wrong during the match, and a failed check stops the result from counting. Agreement stops one player from faking a result, but players who team up can still agree on a false one, and a player who leaves before reporting doesn't count towards the majority. [Reports and bans](https://oss.nekolab.studio/trill/moderation/) are there for players who abuse ranked play. A game that must have results nobody can fake needs a server that runs the match itself, which Purr doesn't provide.
