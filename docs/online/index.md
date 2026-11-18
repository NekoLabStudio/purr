# Online services

Purr's online services keep player accounts, cloud saves and ratings, and run lobbies and matchmaking. They store no email addresses, passwords or raw platform IDs. You run them on your own Cloudflare account, on Workers (with D1, R2 and Durable Objects). Small games fit the Workers Free plan with the `smtp2go` email driver; Cloudflare's own email sending needs Workers Paid.

Players sign in through their platform without seeing an account at all. To bring their progress to another platform, they add their email address: the service emails them a 6-digit code, and every platform signed in with that address shares one account. Until then, their saves stay in the platform's own cloud and the service keeps none.

Purr's `Account`, `Saves`, `Lobbies` and the rest use it when a game sets `online/url`. Its [protocol](protocol.md) is plain HTTP, JSON and WebSocket, so a client for any engine is straightforward to write.

The service's code is in `online/` in Purr's repository.

## Setting up your own

You need a Cloudflare account, Node.js 20 or later and git. In an empty folder, run:

```sh
curl -fsSL https://oss.nekolab.studio/purr/online/setup.sh | sh
```

It downloads the online services from Purr's newest release, signs you in to Cloudflare and asks about your games and how to send email. Then it creates the database and the saves bucket, fills in `wrangler.jsonc`, deploys, and generates and stores the secrets. Last, it makes the folder a git repository and offers to push it to a new private GitHub repository. It prints the service's address, which goes in each game's `online/url`. Keep the identity secret it shows somewhere safe (see [Identity secret](#identity-secret)).

The folder is yours from then on. Add [your own email templates](#email-templates) there, and change its settings with `npm run configure` (below). Running `npm run setup` again is safe, and `npm run setup -- --dry-run` prints what it would do.

## Changing settings

```sh
npm run configure
```

This shows a menu of what you can change: your games and their sign-ins (Steam, Game Center, Play Games), email, TURN, moderation, how long data is kept, save and rate limits, and rotating the identity secret. Each question shows the current value, which Enter keeps. Secrets are never shown, only whether they're set. Nothing changes until you confirm the summary at the end; then it writes `wrangler.jsonc`, stores the secrets and offers to deploy. `npm run configure -- --dry-run` shows what it would change without changing it.

You can also edit `wrangler.jsonc` yourself and deploy with `npx wrangler deploy`. Every setting is listed under [Settings](#settings).

## Updating

```sh
npm run update
```

This brings in Purr's newest stable release (`npm run update -- v1.2.3` picks one). It replaces the files that come from Purr (`src/`, `migrations/`, the tests and scripts) and keeps your own: `src/email-templates/custom/` and your secrets.

Your `wrangler.jsonc` stays yours, with what the release adds to it: new settings with their defaults, new rate limits and anything the service's code needs set up. Where the release changed a default you never touched, yours follows it. A setting you changed yourself is never changed: if the release's default for it moved, the update lists it so you can decide. It also lists settings the release no longer uses, and any new resource (a database or bucket) you'd need to create, which the release's upgrade notes explain.

Before applying new database migrations, it notes where the database stands and prints the command that brings it back there, in case anything goes wrong (D1 keeps 30 days of history). Then it offers to deploy. Commit the changes with git afterwards, so your folder's history shows what each update did.

## Setting up by hand

From the online services' folder (`online/` in Purr's repository):

```sh
npm install
npx wrangler login
npx wrangler d1 create purr-online          # put the database_id it prints into wrangler.jsonc
npx wrangler d1 migrations apply purr-online --remote
npx wrangler r2 bucket create purr-online-saves
npx wrangler r2 bucket lifecycle add purr-online-saves discarded-saves discarded/ --expire-days 30
```

Set the secrets (each command asks for the value):

```sh
npx wrangler secret put IDENTITY_SECRET     # a long random string; see "Identity secret" below
npx wrangler secret put SESSION_SECRET      # another long random string
```

And the optional ones, for the sign-ins, email and TURN you use:

| Secret | For |
|---|---|
| `SMTP2GO_API_KEY` | Sending codes through SMTP2GO (`EMAIL_DRIVER` `smtp2go`) |
| `STEAM_WEB_API_KEY` | Steam sign-in: a publisher Web API key from Steamworks |
| `PLAY_GAMES_CLIENTS` | Google Play Games sign-in: JSON, `{"<game>": {"client_id": "...", "client_secret": "..."}}`, each game's OAuth web client |
| `TURN_KEY_ID`, `TURN_KEY_API_TOKEN` | Cloudflare's TURN service, from Realtime in the dashboard (see TURN below) |
| `TURN_SERVERS` | Or your own TURN servers: a JSON array of `RTCIceServer` objects |

List your games under `GAMES` in `wrangler.jsonc` (the id is what the game sends, and saves are kept per game), and set up email (below). Then deploy:

```sh
npx wrangler deploy
```

`npm test` runs the service's tests.

### Email

Codes go out through the driver `EMAIL_DRIVER` names, from the address in `EMAIL_FROM` (`"Name <address>"`):

- **`cloudflare`** (the default): Cloudflare's Email Sending, through the `send_email` binding in `wrangler.jsonc`. It needs the Workers Paid plan and your domain onboarded under Email Service > Email Sending (Cloudflare adds the SPF, DKIM and DMARC records).
- `smtp2go`: SMTP2GO's HTTP API, with the `SMTP2GO_API_KEY` secret and a sender verified in your SMTP2GO account. `SMTP2GO_API_URL` can pin a region, such as `https://eu-api.smtp2go.com/v3`. Remove the `send_email` binding if you're on the Free plan.
- `log`: writes the email to the console instead of sending it, for `wrangler dev` (put `EMAIL_DRIVER=log` in `.dev.vars`). It only works on your own computer.

Another provider is one file in `src/email/` with one function, listed in `src/email/index.ts`.

### Email templates

The emails' wording and look are files in `online/src/email-templates/`: for each kind of email, a `.txt` file (a `Subject:` line, a blank line, then the plain-text body) and an `.html` file (the HTML body), with `layout.html` around every HTML body. `sign_in` is for signing in, adding or changing an address, and `guest` is for a guest bringing their profile to someone else's device. They can use `{{code}}`, `{{game}}` (the game's `name` in `GAMES`) and `{{minutes}}`. `moderation` is [the daily email to you](moderation.md#a-daily-email) about reported players, with `{{count}}` and `{{players}}`; it isn't for one game, so only `custom/` overrides it. The layout has `{{subject}}`, `{{content}}` and `{{game}}`.

To make them your own, put files with the same names in `online/src/email-templates/custom/`, or in `custom/<game>/` for one game, and deploy. Each file you add replaces the default of the same name, and the defaults never change your files, so updating the service keeps your versions.

### TURN

When two players can't connect directly (behind strict firewalls, or with `lobbies/relay_only` on), their connection goes through a TURN relay. With `TURN_KEY_ID` and `TURN_KEY_API_TOKEN` set, players signed in through Steam, Game Center or Play Games get credentials for Cloudflare's TURN service as they join a lobby, valid for 12 hours, and nobody gets them otherwise; with `TURN_SERVERS`, your own servers. With neither, players get only Cloudflare's STUN server, which helps them find a direct route but can't relay.

TURN is the one part of the service that costs you by the gigabyte, so by default only players a platform has verified get it: nobody can make accounts in bulk to relay their own traffic on your bill. Players with a device account or an email alone get STUN and connect directly where they can. A game with many such players (one on Apple TV without Game Center, say) can give TURN to everyone with `turn_needs_platform` set to `false`.

### Identity secret

Email addresses and platform IDs are only stored as an HMAC made with `IDENTITY_SECRET`. **If you lose it, no existing player can be matched to their account again.** Keep a copy somewhere safe.

To replace it, use Rotate the identity secret in `npm run configure`. It asks for the current secret and checks it against an email address you have an account with, so a mistyped one can't lose your players. Then it keeps the current one as `IDENTITY_SECRET_PREVIOUS`, makes a new `IDENTITY_SECRET`, raises `IDENTITY_KEY_VERSION`, deploys and shows the new secret to keep. Each player is matched under the old secret the next time they sign in and saved again under the new one. It won't rotate again while players are still waiting to move from the last rotation.

### Session secret

`SESSION_SECRET` signs sign-in tokens and keys the email codes. Replacing it signs everyone out (games sign in again on their own) and voids codes waiting to be used; nothing else is lost. Replace it straight away if it may have leaked.

## Settings

All in `vars` in `wrangler.jsonc`, and all changeable with `npm run configure`:

| Setting | Default | Meaning |
|---|---|---|
| `EMAIL_DRIVER` | `cloudflare` | How codes are emailed (see Email) |
| `EMAIL_FROM` | | The sender of those emails, `"Name <address>"` |
| `INACTIVE_ACCOUNT_DAYS` | 730 | Accounts unused this long are deleted, with their saves |
| `AUTO_BAN` | 3 removals in 30 days; 1, 7, 30 days, then for good | Automatic bans for players removed for cheating (see [Moderation](moderation.md#automatic-bans)) |
| `REPORT_RULES` | Off | Automatic bans and renames once enough players report someone (see [Moderation](moderation.md#acting-on-reports-automatically)) |
| `MODERATION_EMAIL` | Off | Where the daily list of reported players goes (see [Moderation](moderation.md#a-daily-email)) |
| `DEVICE_GRANT_DAYS` | 90 | A device that remembers a guest forgets them after this long unused |
| `GUEST_TOKEN_HOURS` | 12 | How long a brought profile lasts on the host's device |
| `SESSION_DAYS` | 30 | How long a sign-in lasts before the game signs in again |
| `MAX_SAVE_BYTES` | 1048576 | The largest save (1 MB); a game can set its own `max_save_bytes` in `GAMES` |
| `MAX_SAVE_SLOTS` | 8 | Saves per player in each game; a game can set its own `max_save_slots` |
| `MAX_CRASH_REPORT_BYTES_PER_DAY` | 2000000 | Crash reports kept in full for each game a day, in bytes (2 MB; see [Crash reports](crash-reports.md)) |

Each game in `GAMES` can also have a `name`, used in the emails; `min_version` and `max_version`, the game versions that may play online (from each game's `application/config/version`; without them, every version can); `ranked_agreement`, how many players must report the same result before ratings change (`"majority"`, `"all"` or `"host"`); `report_failed_checks`, `false` to stop reporting players who fail a [check or a flag](../api/checks.md); `kick_cheaters`, `true` to remove them from the lobby too; `turn_needs_platform`, `false` to give TURN to every signed-in player rather than only those a platform signed in (see TURN above); `crash_reports`, `false` to turn [crash reports](crash-reports.md) away; and `device_accounts`, `false` for a game that only ships where Steam, Game Center or Play Games signs players in, so nobody can make accounts for it without one. Accounts work across an instance's games, so this only keeps anonymous accounts out of the instance when every game it serves turns them off.

## Retention

| Data | Kept for |
|---|---|
| Accounts | Until the player deletes them, or 2 years unused |
| Device grants (remembered guests) | Lapse after 90 days unused |
| Guest tokens | The session, 12 hours at most |
| Saves set aside | 30 days, by the R2 lifecycle rule |
| Email codes | 10 minutes or 5 wrong tries; the row counting an address's codes for the day, a day |
| Lobbies | Until the last player leaves |
| Reports | 90 days |
| Crash reports | 90 days; a crash nobody has had for 90 days goes with them |
| Removals for cheating | `AUTO_BAN`'s window (30 days) |
| A deleted account's ban | Until the ban ends: its identities' keyed hashes and the ban's end, so signing in again doesn't start clean |
| Ratings and blocks | As long as the account |

A daily Cron Trigger deletes unused accounts (with their saves), lapsed grants, spent codes and old reports and crash reports, then sends the moderation email if `MODERATION_EMAIL` is set.
