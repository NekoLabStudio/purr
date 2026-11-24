# Achievements and leaderboards

```gdscript
Achievements.unlock("first_delivery")
Achievements.set_progress("hundred_deliveries", deliveries, 100)
Leaderboards.submit_score("weekly_tips", 12400)
```

Purr sends these to the platform: Steam's achievements and leaderboards, Game Center's or Play Games'. With the [online services](../online/index.md), leaderboards are also kept there, as one board for every platform. Each ID can have a different name on each platform: list them in the **Achievements** and **Leaderboards** tabs of Project Settings, with each platform's name in its column. An empty name means the platform uses the same ID. Call these after `Account.sign_in()`.

## Achievements

`Achievements.unlock(id)` unlocks an achievement. `Achievements.set_progress(id, current, total)` records progress towards one and unlocks it when `current` reaches `total`. Progress only goes up: a lower value than before is ignored.

How each platform shows progress:

- Game Center shows it as a percentage.
- Play Games shows it for incremental achievements, scaled to the steps set in the Play Console. Other achievements unlock at the total.
- Steam keeps progress in a stat. Give the achievement its stat in the Steam Stat column, and Purr sets the stat to `current`. Steam unlocks the achievement when the stat reaches the target set in Steamworks. Without a stat, the achievement unlocks at the total.

`Achievements.get_all()` returns every achievement the player has made progress on, as `{id: progress}`, from 0 to 1 (1 is unlocked). Use it for the game's own achievements screen. `Achievements.show()` opens the platform's: Steam's overlay, Game Center's or Play Games'. `Achievements.changed` fires when the list changes, including when achievements arrive from the platform or another device.

## Where achievements are kept

Purr keeps each player's achievements with their saves, in a slot of their own that `Saves.get_slots()` leaves out but that counts towards the service's limit on slots. Where there's no platform (a computer without Steam, say), that's where they live. They go wherever the saves go: to Steam Cloud or iCloud, and to the player's account once they [add an email](email.md).

When the player signs in on a platform, Purr loads the achievements the platform has, adds any it didn't know about to the list, and sends the platform the ones it lacks. Achievements earned on another platform, or on a computer without one, unlock there. The platform gets only what's missing, so nothing is sent twice.

Two devices that both changed the list before syncing don't conflict: Purr merges the two lists, keeping each achievement's furthest progress. `Saves.conflict` never fires for achievements.

Platforms only know player 1. A guest who brought their profile has their own achievements, through `LocalPlayer.unlock_achievement()`, `set_achievement_progress()` and `get_achievements()`. They're kept on the guest's account and unlocked on their platform the next time they play on their own device.

## Leaderboards

`Leaderboards.submit_score(leaderboard, score)` keeps the player's best score. For the game's own leaderboard screen:

```gdscript
var loaded = await Leaderboards.get_scores("weekly_tips", Leaderboards.SCOPE_AROUND_PLAYER, 10)
for entry in loaded.scores:
    add_row(entry.rank, entry.name, entry.score, entry.player)
```

The scope is `SCOPE_GLOBAL` (the best scores), `SCOPE_FRIENDS` (the best among the player's friends) or `SCOPE_AROUND_PLAYER` (the scores around the player's own, or the best ones when they have none). Each entry has `rank`, `name`, `score` and `player`, which is true for the player's own. Play Games returns up to 25 scores and the others up to 100. Game Center asks the player to share their friends list the first time a game loads friends' scores.

`Leaderboards.show(leaderboard)` opens the platform's leaderboard screen, or all the game's leaderboards when no ID is given. On Steam, it opens the game's leaderboard pages on Steam's website in the overlay.

## Leaderboards on the online services

With the online services set up and the player signed in to them, each leaderboard is one board shared by every platform: a player on Steam and one on an iPhone see each other's scores. `get_scores()` reads that board, and each score also goes to the platform's own board, so `show()` still works (with the platform's players only).

Set each board up in the Leaderboards tab of Project Settings:

- **Order:** whether higher scores rank first (points) or lower ones (a lap time).
- **Resets:** never, daily, weekly or monthly. A board that resets starts again at midnight UTC (weeks from Monday), and the period before stays readable, for last week's winners:

  ```gdscript
  var winners = await Leaderboards.get_scores("weekly_tips", Leaderboards.SCOPE_GLOBAL, 3, Leaderboards.PERIOD_PREVIOUS)
  ```

- **Lowest Score** and **Highest Score:** scores outside these are turned away, so impossible ones never reach the board. Empty means no limit.

The service takes a board's settings from its first score and keeps them, so a modified copy of the game can't turn a board round. To change them later, use `npm run moderate` on your instance (see [Moderation](../online/moderation.md#leaderboards)), which can also hide a player's scores.

When you play your game from the editor or a debug build, your account is a test player's: your scores show on the board for you, and nobody else sees them. Players' scores from release builds show to everyone. Friends' scores are found through the platform's friends (Steam's, Game Center's or Play Games'), so a player without a platform has none.
