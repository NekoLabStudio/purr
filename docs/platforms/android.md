# Android

On Android, Purr signs players in with Google Play Games and can keep their saves in Play Games' saved games.

## Play Games

Play Games is off until the game sets `play_games/app_id` to its application ID: the number the Play Console shows under Play Games Services for the game. Then:

1. In the Play Console, set up Play Games Services for the game, with its credentials, and add achievements and leaderboards. Give Purr their Play Games IDs in the Achievements and Leaderboards tabs of Project Settings (see [Achievements](../api/achievements.md)).
2. In the Android export preset, turn on Gradle Build > Use Gradle Build. Purr adds Play Games to the build: its plugin, Google's SDK and the application ID. Play Games isn't in exports without the Gradle build, and the export warns about it.

`Account.sign_in()` uses Play Games when the game has it. Play Games signs players in by itself when the game starts; if it hasn't, Google's sign-in screen shows, and if the player dismisses it the game carries on with an account for the device.

The `PlayGames` singleton has what Purr uses and a little more, such as showing Play Games' own achievements and leaderboards screens: see [PlayGames](../reference/PlayGames.md).

To sign Play Games players in to the online services, create an OAuth web client for the game in the same Google Cloud project as its Play Games credentials. Set `play_games/server_client_id` to its client ID, and give the service the client ID and secret in `PLAY_GAMES_CLIENTS` (see [Settings](../online/index.md#settings)). Play Games player IDs are per game, so a player is a different Play Games player in each of a developer's games.

## Saved games

Without the online services, or before a player adds an email, Purr can keep saves in the player's Play Games saved games. Turn on saved games for the game in the Play Console, and `play_games/saved_games` in Project Settings.

Saves go there only while the player is signed in to Play Games. When two devices changed the same save, the game gets `Saves.conflict` as with any other cloud (see [Saves](../api/saves.md#two-devices-two-saves)).
