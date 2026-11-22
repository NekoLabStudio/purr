#pragma once

// Shared between GameCenter and GameCenterMultiplayerPeer (Objective-C++ only).

#import <GameKit/GameKit.h>
#if defined(MACOS_ENABLED)
#import <AppKit/AppKit.h>
typedef NSViewController GameCenterViewController;
#else
#import <UIKit/UIKit.h>
typedef UIViewController GameCenterViewController;
#endif

#include "core/string/ustring.h"

String game_center_string(NSString *p_string);
NSString *game_center_ns_string(const String &p_string);

// Shows one of Game Center's screens over the game, and closes it.
void game_center_present(GameCenterViewController *p_controller);
void game_center_dismiss(GameCenterViewController *p_controller);

// The last invite to a Game Center match the player accepted (not one carrying a crossplay
// lobby's code), handed over once.
GKInvite *game_center_take_invite();

// A friend from GameCenter.load_friends(), by their gamePlayerID, for inviting them.
GKPlayer *game_center_friend(const String &p_player_id);

// The game activity under way (GameCenter.start_party, or a match found by party code), ended
// when another starts.
void game_center_set_party(id p_activity);
