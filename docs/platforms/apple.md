# Apple platforms

On macOS, iOS and Apple TV, Purr signs players in with Game Center and can keep their saves in iCloud.

## Game Center

`Account.sign_in()` uses Game Center when the player is signed in to it. If they aren't, Game Center shows its sign-in screen, and if they dismiss it the game carries on with an account for the device.

Turn on Game Center for the app in App Store Connect and set up achievements and leaderboards there; give Purr their Game Center IDs in the Achievements and Leaderboards tabs of Project Settings (see [Achievements](../api/achievements.md)). On iOS and Apple TV, tick the export preset's Game Center capability.

The `GameCenter` singleton has what Purr uses and a little more: see [GameCenter](../reference/GameCenter.md).

To sign Game Center players in to the online services, list the game's bundle IDs in the service's settings. Game Center identities use the player's team ID, so one player is the same player in all of a developer team's games.

With the online services, `Lobbies.invite_friends()` opens Game Center's invite screen for a crossplay lobby, and a crossplay lobby can be a party friends join from Apple's Games app (see [Game Center parties](../api/lobbies.md#game-center-parties)).

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
