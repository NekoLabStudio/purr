# Live config

Live config is values you change after the game has shipped: a feature flag, a switch to turn something off if it misbehaves, a tuning number, the dates of a seasonal event. You set them on your [online services](../online/index.md) instance, and players' games pick them up without an update.

```gdscript
var bonus: float = LiveConfig.get_value("tip_bonus", 1.0)

func _ready() -> void:
    LiveConfig.changed.connect(func() -> void: bonus = LiveConfig.get_value("tip_bonus", 1.0))
```

Give each key its default in Project Settings, as `live_config/<key>` (`live_config/tip_bonus = 1.0`). `get_value()` returns the instance's value when there is one in effect, else that default, else the default you pass it. The game works the same offline and before the first values arrive.

## When values arrive

The game asks the service the first time it reads a value, then every 15 minutes and whenever it comes back to the front (a phone app reopened, a window focused again). `changed` fires when any value changes. `LiveConfig.refresh()` asks straight away, for a game that wants the newest values before starting something. The last values are kept on the device, so a game started offline uses them. Games that never read a value never ask.

No sign-in is needed: the values are the same for every player. Each running game that reads live config makes about 4 requests an hour, which counts towards your Cloudflare plan's requests.

## Setting values

From your instance's folder:

```sh
npm run live-config                                          # every game's values
npm run live-config -- set <game> tip_bonus 1.5              # from now on
npm run live-config -- set <game> winter_fair true --from=2026-12-24T18:00 --until=2026-12-27
npm run live-config -- remove <game> tip_bonus               # back to the game's default
```

A value is JSON: `true`, `1.5`, `"text"`, `[1, 2]` or `{"a": 1}`. Anything that isn't JSON is taken as text. `--from` and `--until` schedule a value, in UTC: games switch to it and back on time by the service's clock, not the device's, so a player who changes their clock can't start an event early. A key's value without dates is replaced by the next one set without dates; scheduled values are added beside it, and the one that started last is in effect. `remove` with a value's ID (shown in the list) removes only that one. Values that have ended are deleted the next day. Add `--local` to work on the database of `npm run dev`.
