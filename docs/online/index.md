# Online services

Purr's online services keep player accounts, cloud saves and ratings, and run lobbies, matchmaking, moderation and crash reports. They store no email addresses, passwords or raw platform IDs.

Players sign in through their platform (Steam, Game Center, Google Play Games, or a device account) without seeing an account at all. To bring their progress to another platform, they add their email address: the service emails them a 6-digit code, and every platform signed in with that address shares one account. Until then, their saves stay in the platform's own cloud and the service keeps none.

You run your own instance, on your own Cloudflare account. It's a Cloudflare Worker, with D1 for its database, R2 for saves and Durable Objects for lobbies, so there are no servers to look after. Small games fit the Workers Free plan with the `smtp2go` email driver; Cloudflare's own email sending needs Workers Paid. The relays players use when they can't connect directly ([TURN](turn.md)) are the one part billed by the gigabyte.

`Account`, `Saves`, `Lobbies` and the rest use the online services when a game sets `online/url` (see [the API](../api/index.md#with-or-without-the-online-services)). Without it, everything works through each platform instead. The service's code is in `online/` in Purr's repository, and each Purr release has it on its own.

To set up an instance and connect a game to it, start with [Getting started](getting-started.md).

## Versions

Each Purr release comes with its online services, and speaks their protocol. When an instance is older than the game needs, `Account` emits `service_outdated` and online features stop for the session (see [Versions](../api/index.md#versions)). Update the instance to the game's Purr release with `npm run update` in its folder (see [Updating](updating.md)).
