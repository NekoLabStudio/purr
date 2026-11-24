# Stats

Stats count what players do: deliveries made, kilometres driven, fish caught. Each player has their own value, and with the [online services](../online/index.md) every player's add up into a total, with a history by day.

```gdscript
Stats.add("km_driven", distance / 1000.0)

var mine := Stats.get_value("km_driven")
var everyone = await Stats.get_total("km_driven")
var this_week = await Stats.get_history("km_driven", 7)
for day in this_week.days:
    add_bar(day.day, day.added)
```

`Stats.add(id, amount)` adds an amount above 0: stats only go up. `get_value()` is the player's own, straight away. `changed` fires when it changes, including when a higher value arrives from another of the player's devices. `get_total()` and `get_history()` ask the online services, so the player must be signed in.

## On the device first

Stats work like saves: they're kept on the device at once, so they work offline and without the online services. With them, what's been added goes to the service every few minutes and when the game goes to the background or closes, and the player's values come back from it, so they follow the player to every device they sign in on. `Stats.send_now()` sends what's waiting straight away.

## Setting stats up

List each stat in the Stats tab of Project Settings:

- **Most a Day:** the most one player can add in a day. Past it, the rest of that day's additions don't count, so a modified copy of the game can't run a total up. The service keeps the limit the stat's first change came with.
- **Steam Stat:** a Steam stat that gets the player's value too, for Steam's own achievements and stats pages.

Test players (you, in the editor and debug builds) see their own values grow, but don't count in totals.

## What it costs

Each running game that adds to stats sends at most one request every three minutes, and each request writes a few rows to the database. A small game stays well within the Workers Free plan; a large one with many stats changing all the time should check its D1 writes. The history keeps one row for each stat and day, for two years.
