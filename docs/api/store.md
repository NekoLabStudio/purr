# DLC and expansions

```gdscript
if Store.owns("winter_expansion"):
	unlock_winter_islands()

func _on_buy_pressed():
	var result = await Store.purchase("winter_expansion")
	if not result.ok and result.error != "cancelled":
		show_message("The purchase didn't go through.")
```

`Store` sells what a player buys once and keeps: DLC, an expansion, a soundtrack, the full version of a game. It's the same code on Steam, the App Store and Google Play. Purr sells through Steam when the game was started from Steam, the App Store on Apple platforms and Google Play on Android.

Items are never used up: there's no buying coins or gems here. The content itself ships with the game, or as a `.pck` you load with `ProjectSettings.load_resource_pack()` once the player owns it. `Store` says whether they do.

## Setting up the items

List each item in the **Store** tab of Project Settings, by the ID your game uses, with its product on each store:

- **Steam**: the DLC's app ID, from Steamworks.
- **App Store**: the product ID of a non-consumable in-app purchase in App Store Connect.
- **Google Play**: the product ID of a one-time product in the Play Console.

Leave a store empty if the item isn't sold there. Each store has a little more to set up: see [Steam](../platforms/steam.md#dlc), [Apple platforms](../platforms/apple.md#in-app-purchases) and [Android](../platforms/android.md#google-play-store).

## Buying and owning

`Store.owns(id)` says whether the player owns an item. On Steam it's known at once. On the App Store and Google Play, what the player owns arrives a moment after the game starts, and `Store.ownership_changed` fires for each item as it does. It fires again whenever ownership changes: a purchase, one made on another device, or a refund.

```gdscript
func _ready():
	Store.ownership_changed.connect(_update_menu)
	_update_menu("")

func _update_menu(_id):
	$WinterButton.text = "Play" if Store.owns("winter_expansion") else "Buy %s" % Store.get_price("winter_expansion")
```

`Store.purchase(id)` asks the store to sell it. Await it for the result: `ok`, or `error` and `message`. The error is:

- `cancelled`: the player changed their mind.
- `pending`: the purchase goes on outside the game (Ask to Buy, a slow payment). `ownership_changed` fires if it completes.
- `unavailable`: there's no store here, or the item isn't on it.
- `failed`: anything else; `message` says what, for your log.

On Steam, the DLC's store page opens in the overlay, and the result comes when the player closes it. If they've turned the overlay off, the page opens in Steam instead, and the result is `pending`.

`Store.get_price(id)` is the price as the store shows it to this player, in their currency, or an empty string until it's known. Steam doesn't tell games a DLC's price, so on Steam it's always empty: its store page shows it.

## Restoring purchases

The App Store requires games that sell items to have a **Restore Purchases** button. Call `Store.restore()` from it. It asks the store again what the player owns. On Steam and Google Play it's rarely needed, since Purr always loads what the player owns, but it does no harm there.

## Trying it in the editor

There's no store when you run the game from the editor, so `Store` pretends: the items listed in `store/owned_in_editor` (Project Settings) are owned, and purchases succeed at once. Try your game both with and without an item by changing that list.

## Only players who bought the game play online

With the [online services](../online/index.md) (Trill), a game can let only players who bought it play online: saves synced to the service, lobbies, matchmaking and relayed connections. The service checks it at every sign-in, from the copy of the game the player is using: Steam's ticket, the App Store's signed proof of purchase, or Google Play's integrity check. Purr sends the proof itself, so there's nothing to add to your game but a message for players who are turned away:

```gdscript
func _ready():
	Account.not_owned.connect(func(): show_message("Buy the game to play online."))
```

You turn it on for the game on the online service: see [Ownership](https://oss.nekolab.studio/trill/ownership/).
