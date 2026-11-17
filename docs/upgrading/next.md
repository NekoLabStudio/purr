# Unreleased

<!-- unreleased-intro-start (support/prepare-release.sh drops this block at release) -->
The notes for the next release: everything below is in `main` and ships
together when the version is tagged.
<!-- unreleased-intro-end -->

## What's new

- `InputBindings`: players change their controls and keep them, saved in the user folder over your Input Map's defaults. Controllers are found by name rather than number, axes can be calibrated, sets of bindings switch on with contexts, and each local player can have their own controllers and bindings. See [Controls](/modules/controls/).
- Button prompts for whatever controller the player is holding (Xbox, PlayStation, Nintendo, Steam Deck, keyboard and mouse, and Logitech's G29, G920 and G923 wheels), as an `InputPrompt` node, an image or inline in text. Purr ships Kenney's Input Prompts in a vector style and a pixel art one, and games can use their own images.

## Changes to check

- The release's download with the Mac, iOS, Apple TV and Vision Pro export templates is now called `purr-<version>-apple-templates.zip` (it was `macos-templates`). The editor's own download and the `.tpz` file are unchanged.
- Godot's own menu controls (`ui_up`, `ui_accept` and the rest) now only answer controllers: a steering wheel's pedals and steering no longer move the highlight round menus. Bind a wheel's buttons to your own menu actions if players should use them there.
