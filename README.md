# Purr

Purr by NekoLab, based on Godot Engine.

Purr is the game engine behind NekoLab's games, based on Godot. It adds online services, platform integrations and a few features of its own.

Purr stays close to upstream Godot. The Godot source is untouched and Purr's own changes are a few small patches, so new Godot releases come to Purr quickly.

The documentation is at [oss.nekolab.studio/purr](https://oss.nekolab.studio/purr/) (`docs/` in this repository).

## Downloads

On a Mac, `brew install --cask nekolabstudio/tap/purr` installs the editor. Each [release](../../releases) has the editor for macOS (Apple Silicon), Windows and Linux, and export templates for those three, iOS, Apple TV, Apple Vision Pro and Android. To install the templates, open the editor and go to Editor > Manage Export Templates > Download and Install, which downloads them from the release, or Install from File with the release's `.tpz`.

## What's built in

- Accounts, saves (local first, synced to the cloud), achievements, leaderboards, lobbies, friends and local players, the same on every platform (`Account`, `Saves`, `Achievements` and the rest).
- Online services you run on your own Cloudflare account (`online/`), for crossplay lobbies, ranked matchmaking, progress across platforms by email and guests who bring their own profile.
- Steam (GodotSteam), Game Center, iCloud saves and Google Play Games.
- WebRTC, built into the engine.
- Controls players can change and keep, with button prompts for whatever controller they're holding, and controllers for each local player.
- Force feedback and hat switches for steering wheels, on macOS and Windows.
- VR on a Mac through Godot's OpenXR: Meta Quest and other headsets with Valve's Steam Link app, and Apple Vision Pro.
- Apple TV exports.
- Parametric, a film-like tonemapper and the default for new environments.
- Screen effects: motion blur, light shafts, lens flares, vignette, film grain and chromatic aberration.
- Recommended graphics settings for the player's computer, with a benchmark to check them, and dynamic resolution.
- Large worlds: a floating origin, streaming, static scenery merged into a few meshes with occluders, collision for instanced props, thousands of moving copies drawn in chunks, and pools of lights and reflection probes round the player.
- Debug drawing (shapes, text and values from anywhere, in 3D and 2D) and an in-game console for your own commands.
- Behaviour trees and state machines for characters' decisions, written in GDScript, with a tree editor that shows them working live.
- Virtual cameras that follow, orbit, ride a rail or frame a group, blending as they take over, with autofocus and walls that fade out of the way.
- Terrain you sculpt and paint in the editor, with textures, holes, scattered grass and rocks, and an ocean.
- Scattering: rocks, grass and debris dropped onto whatever is below a box, by density, slope and scale, solid if you like.
- A time of day that moves the sun, the moon in its phases and the stars across the sky, lights that come on at dusk, and weather: rain, snow, fog and storms that come and go, wet ground with puddles, and snow that settles.
- Water (lakes, rivers and an endless sea), trails of tyre marks and footprints, plants that bend as things go through them, and windows with rooms behind them.
- An MCP server for AI assistants (Claude Code, Codex, Antigravity, OpenCode): Purr's docs, the project, its checks and tests, and the open editor and running game.
- Crash reports: a run that crashed is noticed on the next start, with a report of what it ran on and how it ended, sent to your online services if the player agrees.
- Options: the player's settings, saved and put into effect, from the window and audio volumes to the game's own.
- Tests (`purr --run-tests`), with timings of heavy work against the last run, and a check for broken files (`purr --check-project`), ready for CI.

Each has a small project in `samples/` showing it in use.

Purr is based on a specific Godot release (see [UPSTREAM.txt](UPSTREAM.txt)), so GDExtensions need to be built against it. Purr leaves out a few parts of Godot:

- The Compatibility (OpenGL) renderer. Purr runs on Vulkan, Direct3D 12 and Metal, so there are no web exports.
- Godot Physics for 3D. 3D projects use Jolt, including projects that never chose a physics engine. Godot Physics for 2D is still there.
- Deprecated APIs.

## Building

On a Mac with Xcode and SCons, run `./build.sh` for the editor, `./build.sh templates` for the export templates or `./build.sh tvos` for Apple TV's. [Building from source](https://oss.nekolab.studio/purr/start/building/) has the details, including Steam's SDK.

## How the repository works

| Path | What it is |
|---|---|
| `godot/` | The source of one Godot release, exactly as published. It's never edited by hand. |
| `modules/` | Engine modules built into Purr: Purr's own, and untouched copies of third-party ones such as GodotSteam. |
| `UPSTREAM.txt` | Which release each untouched copy is, with its commit and the source archive's checksum. |
| `patches/` | Purr's changes to Godot and the third-party modules, applied in order at build time. |
| `samples/` | A small project for each module. |
| `docs/` | The documentation, published by `docs-site/build.py`. Upgrade notes are in `docs/upgrading/`. |
| `online/` | The online services (a Cloudflare Worker), deployed on their own. |
| `build/purr.py` | The build options every Purr build uses. |
| `branding/` | Purr's logo, splash screen and icons. |
| `VERSION.txt` | Purr's version. |

Builds apply the patches with `git apply patches/*.patch` and copy the branding over Godot's. `build.sh` takes them off again when it finishes, so `godot/` always matches the release.

### Changing a patch

1. `git switch -c work`
2. `git am --3way patches/*.patch` (each patch becomes a commit)
3. Edit, add or reorder commits on `work`.
4. Delete the files in `patches/`, then run `git format-patch <commit the branch started from>..HEAD -o patches/`.
5. `git switch main`, commit the changed `patches/` folder, and delete `work`.

Each patch's message says why it exists and links the upstream issue, pull request or policy behind it.

### Moving to a new Godot release

1. Download the release's source archive from Godot's GitHub releases. Pre-releases are published in [godotengine/godot-builds](https://github.com/godotengine/godot-builds/releases).
2. Replace the contents of `godot/` with it, update `UPSTREAM.txt`, and commit both as one commit named after the release, like "Godot 4.8".
3. Reapply the patches on a `work` branch, fix any conflicts, drop patches upstream has made unnecessary, and regenerate them.
4. Build and test.

### Releasing

Changes go to `main` through pull requests, which the Test workflow checks and the `purr` team reviews. A change players or game developers need to act on is written into `docs/upgrading/next.md` in the same pull request.

To release, run the Prepare release workflow with the new version (`gh workflow run prepare-release.yml -f version=X.Y.Z`). Once a member of the `purr` team approves it, it sets `VERSION.txt`, turns `next.md` into that version's upgrade notes, commits, tags `vX.Y.Z` and pushes. The tag starts the Release workflow, which builds every platform and publishes the release with the upgrade notes as its description. Releases based on a Godot pre-release are marked as pre-releases. Never change `VERSION.txt` by hand or move a published tag.

The macOS editor is signed and notarised when these secrets are set in the `release-build` environment (which only release tags can use, and only the release workflow can create): `APPLE_CERT_P12` (the Developer ID Application certificate, base64-encoded), `APPLE_CERT_PASSWORD`, and an App Store Connect API key with the Developer role for notarising: `APPLE_NOTARY_KEY_P8` (the .p8 file, base64-encoded), `APPLE_NOTARY_KEY_ID` and `APPLE_NOTARY_ISSUER_ID`. Without them it's signed ad hoc, and macOS will ask for confirmation the first time it opens. Steam's SDK comes from the `STEAMWORKS_SDK_URL` secret.

## License

Purr's code and Godot's are MIT licensed, see [LICENSE.txt](LICENSE.txt). The Purr name and logo aren't covered by that license. Third-party components are listed in [THIRDPARTY.md](THIRDPARTY.md).
