# Moderation

Players can report and block each other from a lobby (see [Lobbies](../api/lobbies.md#removing-reporting-and-blocking-players)). Reports wait in the service's database for you to review, and you can ban players from playing online. You review and act with `npm run moderate`, from your online services' folder. It works on the deployed database through Wrangler, signed in to your Cloudflare account.

## Reviewing reports

```sh
npm run moderate
```

This lists the players reported in the last 90 days, most reporters first: their name, account ID, how many reports from how many players, the reasons and the latest date. Pick one by number to read their reports, with each reporter's name and words, then choose what to do:

- `ban` keeps the player out of multiplayer for a number of days, or `forever`
- `unban` lifts a ban
- `rename` clears their name (see below)
- `dismiss` deletes the reports about them, which takes them off the list

After a ban or a rename, it offers to dismiss the reports too.

The same actions work as commands, for when you already know the account:

```sh
npm run moderate -- show <account ID>
npm run moderate -- find <name>
npm run moderate -- ban <account ID> 30
npm run moderate -- ban <account ID> forever
npm run moderate -- unban <account ID>
npm run moderate -- rename <account ID>
npm run moderate -- dismiss <account ID>
```

`show` also lists the account's recent activity (sign-ins, email changes, devices), which helps when a player says someone else got into their account. Players are identified by their account ID, a random identifier. `find` looks an account up by the name a player gave you. Reports are deleted after 90 days whether you review them or not. Add `--local` to any of these to work on the database of `npm run dev` instead.

A banned player can't create, join or matchmake into lobbies until the ban ends; a lobby they're already in carries on. They can still sign in, play alone and sync their saves. A ban applies to the account, so it reaches every platform the player signs in with. If the player joins their account to another by adding its email address, the ban, their reports and their record go with them. If they delete a banned account, the service keeps its identities' keyed hashes and the ban's end until the ban ends, so signing in again with the same platform account or email brings the ban back. A player using only a device account (no Steam, Game Center or email) can make a new one, so bans hold best against platform accounts.

A renamed player has no name until they get a new one: a platform's name (such as their Steam name) comes back the next time they sign in with it, and otherwise the game asks them to choose (`needs_display_name()`). Until then, other players see them as "Player".

## A daily email

Set `MODERATION_EMAIL` to an address (in Moderation in `npm run configure`, or in `wrangler.jsonc`), and each day the service emails it the players reported in the last day: name, account ID, how many reports from how many players and the reasons. It leaves out what reporters wrote; `npm run moderate` shows that. On a day with no reports, no email goes out. The email uses the same driver as players' codes, and its wording is in `email-templates/moderation.txt` and `moderation.html`, which you can override like the others (see [Email templates](email.md#templates)).

## Automatic bans

In games with `kick_cheaters` on, a lobby removes players who fail a [check or a confirmed flag](../api/checks.md). The `AUTO_BAN` setting turns repeated removals into bans. Change it in Moderation in `npm run configure`, or in `wrangler.jsonc`:

```jsonc
"AUTO_BAN": { "kicks": 3, "within_days": 30, "ban_days": [1, 7, 30, null] }
```

With these defaults, three removals within 30 days ban the player from multiplayer for a day. The next time it's a week, then 30 days, and from then on for good (`null`). Each ban starts the count again, and the account's count of automatic bans sets the next length. Removals by a host never count. Remove `AUTO_BAN` to turn automatic bans off.

## Acting on reports automatically

`REPORT_RULES` bans a player, or clears their name, once enough different players report them. It's off unless you set it, in Moderation in `npm run configure` or in `wrangler.jsonc`:

```jsonc
"REPORT_RULES": {
	"ban": { "reasons": ["cheating", "abuse"], "reporters": 5, "within_days": 7, "min_reporter_age_days": 7, "ban_days": [1, 7, 30, null] },
	"rename": { "reasons": ["name"], "reporters": 3, "within_days": 7, "min_reporter_age_days": 7 }
}
```

With this, reports for cheating or abuse from five different players within a week ban the player, for as long as `ban_days` gives (as with `AUTO_BAN`, and counting the same automatic bans, so a player banned by either gets a longer ban next time). Reports about their name from three different players clear it, as `rename` does. Either rule can be left out.

Each player counts once however many times they report, and only players whose accounts are at least `min_reporter_age_days` old count, so one person can't make a handful of new accounts to get someone banned. Reports a rule acted on don't count again, so the next action takes as many new reports. Reports the anti-cheat makes (`report_failed_checks`) never count here, as `AUTO_BAN` already counts those removals. The reports stay on the list for you to review, marked as acted on.

Players can still team up to report someone unfairly, so keep the thresholds high enough for your games, and check the reports behind automatic bans: `unban` lifts one.

## Leaderboards

A leaderboard on the service is made by its first score, with the order, reset and limits the game's Project Settings gave it (see [Leaderboards](../api/leaderboards.md)). `npm run moderate` shows a board and looks after it:

```sh
npm run moderate -- scores <game> <leaderboard>
npm run moderate -- hide <account ID>
npm run moderate -- hide <account ID> <game> <leaderboard>
npm run moderate -- unhide <account ID>
npm run moderate -- board <game> <leaderboard> lower weekly 30000 none
```

`scores` lists the current period's best 50, with each player's account ID, and marks scores that are hidden, from test players or from banned players. `hide` hides a player's scores, all of them or one board's: only they see them, and their later scores on that board are hidden too. Banned players' scores are hidden the same way until their ban ends. `board` sets which scores rank first (`higher` or `lower`), when the board starts again (`never`, `daily`, `weekly` or `monthly`, at midnight UTC, weeks from Monday) and the lowest and highest scores it takes (a whole number, or `none` for no limit; leaving them out means none). A board takes these from its first score, so if a modified copy of the game got to a board before your game did, set it right here. Scores already made stay where they are, so change a board's order before players have scores on it.

## Test players

Scores from test players are seen only by themselves, and they don't count in totals. A player signed in from the editor or a debug build is a test player from then on, and so are accounts you mark:

```sh
npm run moderate -- test <account ID>
npm run moderate -- untest <account ID>
```

`show` says whether an account is a test player. A player who joins their account to a test player's (by adding its email address) is a test player too.
