# Online services

Purr's online features (accounts that follow players across platforms, cloud saves, crossplay lobbies, matchmaking and ratings, moderation and crash reports) use [Trill](https://oss.nekolab.studio/trill/), NekoLab's online services for games. You run your own instance on your Cloudflare account; setting one up takes one command:

```sh
curl -fsSL https://oss.nekolab.studio/trill/setup.sh | sh
```

It prints your instance's address. Put it in Project Settings as `trill/url`, and the game's ID from your instance's `GAMES` as `trill/game`. From then on, `Account`, `Saves`, `Lobbies` and the rest use Trill whenever the player is signed in to it (see [the API](../api/index.md#with-or-without-the-online-services)). Without `trill/url`, everything works through each platform instead.

Trill's documentation covers the rest: [setting it up](https://oss.nekolab.studio/trill/getting-started/), [connecting games](https://oss.nekolab.studio/trill/connecting/), [settings](https://oss.nekolab.studio/trill/settings/), [TURN](https://oss.nekolab.studio/trill/turn/), [checking players own the game](https://oss.nekolab.studio/trill/ownership/), [moderation](https://oss.nekolab.studio/trill/moderation/), [crash reports](https://oss.nekolab.studio/trill/crash-reports/) and [privacy](https://oss.nekolab.studio/trill/privacy/).

## Versions

Each Purr release speaks one version of Trill's protocol, and Trill says which it speaks on every answer. When an instance is too old for the game, `Account` emits `service_outdated` and online features stop for the session (see [Versions](../api/index.md#versions)). Update the instance with `npm run update` in its folder. This Purr needs Trill 0.1.0 or later.
