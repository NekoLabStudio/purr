# Steam

Purr includes [GodotSteam](https://godotsteam.com), so the `Steam` singleton is there in every project, and Purr adds Steam's library to desktop exports. Set the game's App ID in Project Settings under `steam/initialization/app_data/app_id`.

When Steam is running, `Account.sign_in()` signs in with the Steam account, starting Steamworks if the game hasn't. Through Purr, Steam gives you:

- the player's name, and their Steam account as their identity on the online services;
- saves in Steam Cloud (turn on Steam Cloud for the game in Steamworks);
- achievements and leaderboards, set up in Steamworks and named in the Achievements and Leaderboards tabs of Project Settings (see [Achievements](../api/achievements.md));
- lobbies and invites: Steam lobbies without the online services, and Steam invites to crossplay lobbies with them.

For anything else, use the `Steam` singleton directly. GodotSteam's [documentation](https://godotsteam.com) covers it.

## Trying it before release

Keep Steam running and set the App ID, and the game signs in with Steam when you run it from the editor too. Before you have an App ID of your own, Valve's test App ID `480` works for signing in, lobbies and invites. Without Steam running, the game signs in with an account for the device instead.

## Sign-in for the online services

To sign Steam players in to the online services, the service needs to know the game's Steam App ID and a publisher Web API key. See [the online services](../online/index.md).
