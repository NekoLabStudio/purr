# Local players

Player 1 is the player signed in on the device. Everyone else playing on the same device is a guest, in `LocalPlayers.players` after player 1.

```gdscript
var guest: LocalPlayer = LocalPlayers.add_guest()
```

There are a few kinds of guest, in `guest.kind`:

| Kind | What it is | Its saves |
|---|---|---|
| `KIND_GUEST` | A plain guest | None of their own: their progress belongs in player 1's save |
| `KIND_LOCAL_PROFILE` | A guest remembered on this device | Their own, in `guest.saves`, kept like player 1's (the platform's cloud, or this device) |
| `KIND_ONLINE_GUEST` | A guest who brought their profile with their email | Their own account's, in `guest.saves`, which this device can only load and save |

For co-op, keep progress that belongs to the game world in player 1's save and progress that belongs to a character in each player's.

## Names

A plain guest is called "Guest" until the game names them: `guest.needs_display_name()` is true until then, and `guest.set_display_name("Player Two")` names them for as long as they play. A local profile keeps the name it was remembered with, and renaming it keeps the new name on this device. A guest who brought their profile has their account's name. This device can't change it (`set_display_name()` returns `error` `"own_device"`); they change it on their own device. When a guest brings a profile for an address that has no account yet, the `name` given to `LocalPlayers.confirm_guest()` becomes their account's name.

In online play, only player 1's name reaches the lobby. Share guests' names with the other players through the game's own multiplayer messages.

## Remembering a guest

```gdscript
await guest.remember("Inês")
```

A plain guest becomes a local profile, kept on this device and listed in `LocalPlayers.remembered` next time, for a "Who's playing?" screen. Add one back with `LocalPlayers.add_guest(remembered)`. A local profile needs no account. Later, the person can add their email (`guest.link_email()` and `guest.confirm_email()`) to keep their progress on their own account.

## Bringing your profile

A guest with their own account (any player who has added an email) can play with their own progress on someone else's device:

```gdscript
await LocalPlayers.invite_guest(address)
var code: String = await ask_guest_for_code()
var result = await LocalPlayers.confirm_guest(code, remember)
var guest: LocalPlayer = result.guest
```

The code goes to the guest's email. It lets this device load and save their games, but it can't sign in to their account or change it, which the email explains. With `remember`, this device keeps them for next time, until the guest revokes it from their own device. `LocalPlayers.guest_added` fires when they've joined.

`guest.forget()` forgets a remembered guest on this device. A local profile lives only on this device, so forgetting it deletes its progress.

## Each player's menus

Godot gives a window one focused control, so one player opening a menu would take another's focus. Put each player's menus in their own `SubViewport` with `local_focus` on, and each viewport keeps its own focus:

```gdscript
var viewport := SubViewport.new()
viewport.local_focus = true
```

Send each player's controller input to their own viewport only (with `push_input`), or every menu moves at once. Two players typing at once, or with dropdowns open at once, share the window's one text input and popup. `samples/split_focus` shows two players' menus side by side.
