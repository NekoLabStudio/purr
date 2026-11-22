# Apple platforms

On macOS, iOS and Apple TV, Purr signs players in with Game Center, can keep their saves in iCloud and sells through the App Store.

## Game Center

`Account.sign_in()` uses Game Center when the player is signed in to it. If they aren't, Game Center shows its sign-in screen, and if they dismiss it the game carries on with an account for the device.

Turn on Game Center for the app in App Store Connect and set up achievements and leaderboards there; give Purr their Game Center IDs in the Achievements and Leaderboards tabs of Project Settings (see [Achievements](../api/achievements.md)). On iOS and Apple TV, tick the export preset's Game Center capability.

The `GameCenter` singleton has what Purr uses and a little more: see [GameCenter](../reference/GameCenter.md).

To sign Game Center players in to the online services, list the game's bundle IDs in the service's settings. Game Center identities use the player's team ID, so one player is the same player in all of a developer team's games.

Without the online services, lobbies and matchmaking are Game Center matches: players play through Apple's servers, invite friends with Game Center's invite screen, and, with a party activity set up, join with a code (see [Lobbies](../api/lobbies.md#game-center)). With the online services, `Lobbies.invite_friends()` opens Game Center's invite screen for a crossplay lobby, and a crossplay lobby can be a party friends join from Apple's Games app (see [Game Center parties](../api/lobbies.md#game-center-parties)).

`Friends.get_all()` lists the player's Game Center friends, once they agree to share them. Game Center asks with a reason the game gives: Purr's iOS, Apple TV and visionOS exports add one ("Shows your Game Center friends in the game, so you can invite them."). On macOS, a game that lists friends adds it to the export preset's Application > Additional Plist Content:

```xml
<key>NSGKFriendListUsageDescription</key>
<string>Shows your Game Center friends in the game, so you can invite them.</string>
```

## iCloud saves

Without the online services, or before a player adds an email, Purr can keep saves in the player's iCloud through CloudKit. It's off until the game names its iCloud container:

1. In Apple's developer portal, create an iCloud container for the app (such as `iCloud.com.example.game`) and add it to the app's provisioning profile.
2. Set `icloud/container` to the container's ID.
3. Give the export preset these entitlements, in `entitlements/additional` (iOS and Apple TV) or `codesign/entitlements/additional` (macOS):

```xml
<key>com.apple.developer.icloud-container-identifiers</key>
<array><string>iCloud.com.example.game</string></array>
<key>com.apple.developer.icloud-services</key>
<array><string>CloudKit</string></array>
```

A Mac game distributed outside the App Store also needs `com.apple.developer.icloud-container-environment` set to `Production`. Purr's export warns when the entitlements are missing.

Saves go to iCloud only when the player is signed in to iCloud on the device. When two devices changed the same save, the game gets `Saves.conflict` as with any other cloud (see [Saves](../api/saves.md#two-devices-two-saves)). Where Steam is running, on a Mac, Steam Cloud keeps the saves instead.

## In-app purchases

`Store` sells through the App Store with StoreKit (see [DLC and expansions](../api/store.md)). Create each item in App Store Connect as a non-consumable in-app purchase and give Purr its product ID in the Store tab of Project Settings. On iOS and Apple TV, tick the export preset's In-App Purchase capability. Apple requires a **Restore Purchases** button that calls `Store.restore()`.

Only a copy from the App Store or TestFlight can buy anything. On a Mac, a copy from anywhere else (Steam, or your own website) has no App Store: Steam sells there when Steam is running, and otherwise `Store.is_available()` is false.

To test purchases before release, use a sandbox account (App Store Connect, under Users and Access) on a TestFlight build. A build run from Xcode can buy with a sandbox account too, but it can't prove to the online services that it was bought, since Apple doesn't sign it.

The same proof of purchase, StoreKit's AppTransaction, is what Purr sends to the online services for games that only play online for players who bought them (see [Ownership](https://oss.nekolab.studio/trill/ownership/)). TestFlight copies count unless the game's `test_builds` is `false`.
