# Accounts, saves and play together

Every game needs the same few things: signing in, saves, achievements, leaderboards, lobbies, friends, local players and, for many, something to buy. Purr has a singleton for each, and the same game code works on Steam, Game Center, Play Games, a device without a platform and, when you run them, the [online services](../online/index.md) (Trill):

| Singleton | What it does |
|---|---|
| `Account` | Signing in, the player's name, their [email](email.md) and deleting their account |
| `Saves` | [Saves](saves.md), on disk first and synced in the background |
| `Achievements`, `Leaderboards` | [Achievements and leaderboards](achievements.md) on every platform |
| `Lobbies` | [Playing together](lobbies.md), [ranked matches](ranked.md) and [fair play](checks.md) |
| `Friends` | The player's Steam friends and their lobbies |
| `LocalPlayers` | [Guests](local-players.md) playing on the same device |
| `Store` | [DLC and expansions](store.md) the player buys once, on Steam, the App Store and Google Play |

Underneath, Purr uses each platform's own systems, and each platform's singleton (`Steam`, `GameCenter`, `PlayGames`, `ICloud`) stays available for anything Purr doesn't cover.

## Signing in

```gdscript
var result = await Account.sign_in()
print(Account.player_name)
```

`sign_in()` picks the platform: Steam when Steam is running, Game Center on Apple platforms when the player is signed in to it, Play Games on Android when the game [has it](../platforms/android.md), and otherwise an account for this device. `Account.provider` says which. The result has `ok`, `provider`, `player_name` and `online`, which is `false` when the online services couldn't be reached. Sign-in still succeeds offline, and saves sync the next time the game is online.

Calls that wait on a platform or the online service return a signal to await. Its result is a dictionary with `ok`, and when it failed, `error` (a short code, listed in the class reference for each call) and `message` (for your log, not the player).

## The player's name

Signed in with Steam, Game Center or Play Games, the player's name is the platform's. Without a platform (a device account, or no online services), `Account.needs_display_name()` is true until they choose one: ask them, and save it with `Account.set_display_name()`.

```gdscript
if Account.needs_display_name():
	var name: String = await ask_player_for_name()
	var saved = await Account.set_display_name(name)  # 1 to 24 characters
```

A chosen name is kept on the player's account and stays theirs on every platform they sign in with later, instead of the platform's. Other players see it in lobbies, so they can report a name that breaks your rules (see [Moderation](https://oss.nekolab.studio/trill/moderation/)).

## With or without the online services

Purr works without the online services. When `trill/url` is empty in Project Settings, the platform account is the player's identity, saves go to the platform's cloud, and lobbies, matchmaking, invites and friends are the platform's own: Steam lobbies, or Game Center matches on Apple platforms (see [Lobbies](lobbies.md#without-the-online-services)). The same calls work either way. Set the URL, and the game in `trill/game`, to add crossplay lobbies, progress that follows the player to other platforms (by [email](email.md)) and guests who bring their own profile.

## What works where

Each singleton says what works where the game is running, so the game can show only what does:

| Ask | True |
|---|---|
| `Achievements.is_available()`, `Leaderboards.is_available()` | on Steam, Game Center and Play Games (achievements are kept with the player's saves everywhere) |
| `Lobbies.is_available()`, `Lobbies.can_matchmake()` | with the online services, on Steam, or on Game Center |
| `Lobbies.has_crossplay()`, `Account.can_link_email()`, `LocalPlayers.can_invite_guests()` | with the online services, signed in |
| `Lobbies.can_invite()` | on Steam and Game Center |
| `Friends.is_available()` | on Steam, Game Center and Play Games |
| `Store.is_available()` | on Steam, the App Store and Google Play, and in the editor |

`Saves.get_backend()` says where saves sync. Calling something that isn't available does nothing.

## Deleting an account

Stores ask games with accounts to let players delete them. `Account.delete_account()` deletes the player's account and everything in it on the online services, and signs them out here; signing in again starts a new account. Their saves on the device stay.

## Versions

Every request to the online services says which game it's from and its version, from `application/config/version` in Project Settings. The service's operator can set a minimum and a maximum version for each game (see [Settings](https://oss.nekolab.studio/trill/settings/)); without them, every version can play online. When the service turns a version away, `Account` emits `update_required` with the allowed range, and online features stop for the session. Ask the player to update; their saves keep working on the device and in the platform's cloud.

```gdscript
Account.update_required.connect(func(min_version, max_version): show_update_prompt())
```

It works the other way too. If the service is older than this version of Purr needs, `Account` emits `service_outdated` and online features stop the same way.

The class reference has every method: [Account](../reference/Account.md), [Saves](../reference/Saves.md), [Achievements](../reference/Achievements.md), [Leaderboards](../reference/Leaderboards.md), [Lobbies](../reference/Lobbies.md), [Friends](../reference/Friends.md), [LocalPlayers](../reference/LocalPlayers.md), [LocalPlayer](../reference/LocalPlayer.md) and [PlayerSaves](../reference/PlayerSaves.md). They're also in the editor, under Help > Search Help.
