// Apple's headers first: GameController's (which GameKit brings in) use the name Key, which
// Godot also defines.
#import <GameKit/GameKit.h>
#if defined(MACOS_ENABLED)
#import <AppKit/AppKit.h>
#else
#import <UIKit/UIKit.h>
#endif

#include "game_center.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

GameCenter *GameCenter::singleton = nullptr;

void GameCenterCall::_bind_methods() {
	ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::DICTIONARY, "result")));
}

namespace {

String ns_string(NSString *p_string) {
	return p_string ? String::utf8(p_string.UTF8String) : String();
}

// Every call's result: ok, and on failure a short error code and Game Center's own message.
Dictionary result(NSError *p_error) {
	Dictionary dictionary;
	dictionary["ok"] = p_error == nil;
	if (p_error) {
		const bool game_center = [p_error.domain isEqualToString:GKErrorDomain];
		dictionary["error"] = game_center && p_error.code == GKErrorNotAuthenticated ? "not_signed_in" : game_center && p_error.code == GKErrorCancelled ? "cancelled" : "failed";
		dictionary["message"] = ns_string(p_error.localizedDescription);
	}
	return dictionary;
}

// Game Center's handlers run on any thread: results go back to Godot's main thread.
void finish(const Ref<GameCenterCall> &p_call, const Dictionary &p_result) {
	callable_mp(GameCenter::get_singleton(), &GameCenter::finish_call).call_deferred(p_call, p_result);
}

// Shows one of Game Center's screens (sign-in, invites) over the game, and closes it.
#if defined(MACOS_ENABLED)
void present(NSViewController *p_controller) {
	NSWindow *window = NSApp.keyWindow ?: NSApp.mainWindow;
	[window.contentViewController presentViewControllerAsSheet:p_controller];
}

void dismiss(NSViewController *p_controller) {
	[p_controller.presentingViewController dismissViewController:p_controller];
}
#else
void present(UIViewController *p_controller) {
	for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
		if ([scene isKindOfClass:[UIWindowScene class]]) {
			UIViewController *root = ((UIWindowScene *)scene).keyWindow.rootViewController;
			if (root) {
				[root presentViewController:p_controller animated:YES completion:nil];
				return;
			}
		}
	}
}

void dismiss(UIViewController *p_controller) {
	[p_controller dismissViewControllerAnimated:YES completion:nil];
}
#endif

void emit_on_main_thread(const StringName &p_signal, const Variant &p_value) {
	GameCenter::get_singleton()->call_deferred(SNAME("emit_signal"), p_signal, p_value);
}

} // namespace

// Invites and parties arriving from Game Center, and the invite screen's outcome. GameKit keeps
// only weak references to both, so one of each lives as long as the game.
@interface GameCenterListener : NSObject <GKLocalPlayerListener, GKMatchmakerViewControllerDelegate, GKGameCenterControllerDelegate>
@end

@implementation GameCenterListener

// The player accepted an invite (in Messages or a notification): the inviting game's player group
// says what to join. Connecting to the invite's match lets the inviter's invite screen close.
- (void)player:(GKPlayer *)player didAcceptInvite:(GKInvite *)invite {
	const int64_t group = invite.playerGroup;
	[GKMatchmaker.sharedMatchmaker matchForInvite:invite
								completionHandler:^(GKMatch *match, NSError *error) {
									[match disconnect];
								}];
	emit_on_main_thread(SNAME("invite_accepted"), group);
}

- (void)player:(GKPlayer *)player wantsToPlayGameActivity:(GKGameActivity *)activity completionHandler:(void (^)(BOOL handled))completionHandler API_AVAILABLE(ios(26.0), macos(26.0), tvos(26.0), visionos(26.0)) {
	if (activity.partyCode) {
		emit_on_main_thread(SNAME("party_joined"), ns_string(activity.partyCode));
	}
	completionHandler(activity.partyCode != nil);
}

- (void)matchmakerViewControllerWasCancelled:(GKMatchmakerViewController *)viewController {
	dismiss(viewController);
}

- (void)matchmakerViewController:(GKMatchmakerViewController *)viewController didFailWithError:(NSError *)error {
	dismiss(viewController);
}

- (void)gameCenterViewControllerDidFinish:(GKGameCenterViewController *)gameCenterViewController {
	dismiss(gameCenterViewController);
}

// An invited player connected: the game itself goes on in the inviter's lobby.
- (void)matchmakerViewController:(GKMatchmakerViewController *)viewController didFindMatch:(GKMatch *)match {
	[match disconnect];
	dismiss(viewController);
}

@end

namespace {

GameCenterListener *listener = nil;
id party = nil; // The GKGameActivity under way, on OS 26 and later.

} // namespace

GameCenter::GameCenter() {
	singleton = this;
}

GameCenter::~GameCenter() {
	singleton = nullptr;
}

Ref<GameCenterCall> GameCenter::begin_call() {
	Ref<GameCenterCall> call;
	call.instantiate();
	pending.insert(call);
	return call;
}

void GameCenter::finish_call(const Ref<GameCenterCall> &p_call, const Dictionary &p_result) {
	p_call->emit_signal(SNAME("completed"), p_result);
	pending.erase(p_call);
}

void GameCenter::finish_sign_in(const Dictionary &p_result) {
	authenticating = false;
	last_answer = p_result;
	for (const Ref<GameCenterCall> &call : waiting_for_sign_in) {
		finish_call(call, p_result);
	}
	waiting_for_sign_in.clear();
}

Signal GameCenter::authenticate() {
	const Ref<GameCenterCall> call = begin_call();
	const Signal completed(call.ptr(), SNAME("completed"));
	if (GKLocalPlayer.local.isAuthenticated) {
		Dictionary done = result(nil);
		done["player_name"] = get_player_name();
		finish(call, done);
		return completed;
	}
	if (handler_installed && !authenticating) {
		// Already asked this run, and the player isn't signed in: GameKit won't ask again until
		// their state changes, so the last answer stands.
		finish(call, last_answer);
		return completed;
	}
	waiting_for_sign_in.push_back(call);
	if (authenticating) {
		return completed;
	}
	authenticating = true;
	handler_installed = true;
	// Game Center calls this again whenever the player's state changes; only the first
	// answer after asking settles the calls waiting on it.
	GKLocalPlayer.local.authenticateHandler = ^(id controller, NSError *error) {
		if (controller) {
			present(controller);
			return;
		}
		if (GKLocalPlayer.local.isAuthenticated && !listener) {
			listener = [GameCenterListener new];
			[GKLocalPlayer.local registerListener:listener];
		}
		Dictionary done = result(GKLocalPlayer.local.isAuthenticated ? nil : error);
		if (!GKLocalPlayer.local.isAuthenticated && !error) {
			done["ok"] = false;
			done["error"] = "not_signed_in";
			done["message"] = "The player isn't signed in to Game Center.";
		}
		done["player_name"] = ns_string(GKLocalPlayer.local.displayName);
		callable_mp(GameCenter::get_singleton(), &GameCenter::finish_sign_in).call_deferred(done);
	};
	return completed;
}

bool GameCenter::is_authenticated() const {
	return GKLocalPlayer.local.isAuthenticated;
}

String GameCenter::get_player_name() const {
	return GKLocalPlayer.local.isAuthenticated ? ns_string(GKLocalPlayer.local.displayName) : String();
}

String GameCenter::get_team_player_id() const {
	return GKLocalPlayer.local.isAuthenticated ? ns_string(GKLocalPlayer.local.teamPlayerID) : String();
}

Signal GameCenter::fetch_identity_verification() {
	const Ref<GameCenterCall> call = begin_call();
	const Signal completed(call.ptr(), SNAME("completed"));
	[GKLocalPlayer.local fetchItemsForIdentityVerificationSignature:^(NSURL *public_key_url, NSData *signature, NSData *salt, uint64_t timestamp, NSError *error) {
		Dictionary done = result(error);
		if (!error) {
			// The shape Purr's online services take (PurrOnline.sign_in_game_center).
			done["public_key_url"] = ns_string(public_key_url.absoluteString);
			done["signature"] = ns_string([signature base64EncodedStringWithOptions:0]);
			done["salt"] = ns_string([salt base64EncodedStringWithOptions:0]);
			done["timestamp"] = (int64_t)timestamp;
			done["team_player_id"] = ns_string(GKLocalPlayer.local.teamPlayerID);
			done["bundle_id"] = ns_string(NSBundle.mainBundle.bundleIdentifier);
		}
		finish(call, done);
	}];
	return completed;
}

Signal GameCenter::report_achievement(const String &p_id, double p_percent) {
	const Ref<GameCenterCall> call = begin_call();
	const Signal completed(call.ptr(), SNAME("completed"));
	GKAchievement *achievement = [[GKAchievement alloc] initWithIdentifier:[NSString stringWithUTF8String:p_id.utf8().get_data()]];
	achievement.percentComplete = p_percent;
	achievement.showsCompletionBanner = YES;
	[GKAchievement reportAchievements:@[ achievement ]
				withCompletionHandler:^(NSError *error) {
					finish(call, result(error));
				}];
	return completed;
}

Signal GameCenter::submit_score(const String &p_leaderboard_id, int64_t p_score) {
	const Ref<GameCenterCall> call = begin_call();
	const Signal completed(call.ptr(), SNAME("completed"));
	[GKLeaderboard submitScore:p_score
						context:0
						 player:GKLocalPlayer.local
				 leaderboardIDs:@[ [NSString stringWithUTF8String:p_leaderboard_id.utf8().get_data()] ]
			  completionHandler:^(NSError *error) {
				  finish(call, result(error));
			  }];
	return completed;
}

Signal GameCenter::get_achievements() {
	const Ref<GameCenterCall> call = begin_call();
	const Signal completed(call.ptr(), SNAME("completed"));
	[GKAchievement loadAchievementsWithCompletionHandler:^(NSArray<GKAchievement *> *achievements, NSError *error) {
		Dictionary done = result(error);
		Dictionary progress;
		for (GKAchievement *achievement in achievements) {
			progress[ns_string(achievement.identifier)] = achievement.percentComplete;
		}
		done["achievements"] = progress;
		finish(call, done);
	}];
	return completed;
}

// Up to p_count scores: the best ones ("global"), the best among the player's friends
// ("friends", which asks the player to share their friends list the first time), or those
// around the player's own ("around_player", the best ones if they have none).
Signal GameCenter::get_scores(const String &p_leaderboard_id, Scope p_scope, int p_count) {
	const Ref<GameCenterCall> call = begin_call();
	const Signal completed(call.ptr(), SNAME("completed"));
	const NSUInteger count = CLAMP(p_count, 1, 100);
	const GKLeaderboardPlayerScope scope = p_scope == SCOPE_FRIENDS ? GKLeaderboardPlayerScopeFriendsOnly : GKLeaderboardPlayerScopeGlobal;
	const bool around_player = p_scope == SCOPE_AROUND_PLAYER;
	void (^entries_loaded)(GKLeaderboardEntry *, NSArray<GKLeaderboardEntry *> *, NSInteger, NSError *) = ^(GKLeaderboardEntry *local, NSArray<GKLeaderboardEntry *> *entries, NSInteger total, NSError *error) {
		Dictionary done = result(error);
		Array scores;
		for (GKLeaderboardEntry *entry in entries) {
			Dictionary score;
			score["rank"] = (int64_t)entry.rank;
			score["name"] = ns_string(entry.player.displayName);
			score["score"] = (int64_t)entry.score;
			score["player"] = [entry.player.gamePlayerID isEqualToString:GKLocalPlayer.local.gamePlayerID];
			scores.push_back(score);
		}
		done["scores"] = scores;
		finish(call, done);
	};
	[GKLeaderboard loadLeaderboardsWithIDs:@[ [NSString stringWithUTF8String:p_leaderboard_id.utf8().get_data()] ]
						 completionHandler:^(NSArray<GKLeaderboard *> *leaderboards, NSError *error) {
							 GKLeaderboard *leaderboard = leaderboards.firstObject;
							 if (!leaderboard) {
								 Dictionary done = result(error);
								 done["ok"] = false;
								 if (!error) {
									 done["error"] = "not_found";
									 done["message"] = "Game Center has no leaderboard with that ID.";
								 }
								 finish(call, done);
								 return;
							 }
							 if (!around_player) {
								 [leaderboard loadEntriesForPlayerScope:scope timeScope:GKLeaderboardTimeScopeAllTime range:NSMakeRange(1, count) completionHandler:entries_loaded];
								 return;
							 }
							 // The player's own rank first, then the scores around it.
							 [leaderboard loadEntriesForPlayerScope:scope
														  timeScope:GKLeaderboardTimeScopeAllTime
															  range:NSMakeRange(1, 1)
												  completionHandler:^(GKLeaderboardEntry *local, NSArray<GKLeaderboardEntry *> *entries, NSInteger total, NSError *rank_error) {
													  const NSInteger rank = local ? local.rank : 1;
													  const NSUInteger first = (NSUInteger)MAX((NSInteger)1, rank - (NSInteger)(count / 2));
													  [leaderboard loadEntriesForPlayerScope:scope timeScope:GKLeaderboardTimeScopeAllTime range:NSMakeRange(first, count) completionHandler:entries_loaded];
												  }];
						 }];
	return completed;
}

void GameCenter::show_achievements() {
	ERR_FAIL_COND_MSG(!listener, "Sign in to Game Center (GameCenter.authenticate) before showing achievements.");
	GKGameCenterViewController *controller = [[GKGameCenterViewController alloc] initWithState:GKGameCenterViewControllerStateAchievements];
	controller.gameCenterDelegate = listener;
	present(controller);
}

void GameCenter::show_leaderboard(const String &p_leaderboard_id) {
	ERR_FAIL_COND_MSG(!listener, "Sign in to Game Center (GameCenter.authenticate) before showing leaderboards.");
	GKGameCenterViewController *controller = p_leaderboard_id.is_empty()
			? [[GKGameCenterViewController alloc] initWithState:GKGameCenterViewControllerStateLeaderboards]
			: [[GKGameCenterViewController alloc] initWithLeaderboardID:[NSString stringWithUTF8String:p_leaderboard_id.utf8().get_data()] playerScope:GKLeaderboardPlayerScopeGlobal timeScope:GKLeaderboardTimeScopeAllTime];
	controller.gameCenterDelegate = listener;
	present(controller);
}

void GameCenter::invite_players(int64_t p_player_group, int p_max_players) {
	ERR_FAIL_COND_MSG(!listener, "Sign in to Game Center (GameCenter.authenticate) before inviting players.");
	GKMatchRequest *request = [GKMatchRequest new];
	request.minPlayers = 2;
	request.maxPlayers = MAX(2, MIN(p_max_players, (int)[GKMatchRequest maxPlayersAllowedForMatchOfType:GKMatchTypePeerToPeer]));
	request.playerGroup = p_player_group;
	GKMatchmakerViewController *controller = [[GKMatchmakerViewController alloc] initWithMatchRequest:request];
	ERR_FAIL_NULL_MSG(controller, "Game Center couldn't open its invite screen.");
	controller.matchmakerDelegate = listener;
	controller.canStartWithMinimumPlayers = YES;
	present(controller);
}

void GameCenter::start_party(const String &p_activity_id, const String &p_party_code) {
	end_party();
	if (@available(iOS 26.0, macOS 26.0, tvOS 26.0, visionOS 26.0, *)) {
		NSString *code = [NSString stringWithUTF8String:p_party_code.utf8().get_data()];
		ERR_FAIL_COND_MSG(![GKGameActivity isValidPartyCode:code], vformat("\"%s\" isn't a Game Center party code.", p_party_code));
		NSString *activity_id = [NSString stringWithUTF8String:p_activity_id.utf8().get_data()];
		[GKGameActivityDefinition loadGameActivityDefinitionsWithIDs:@[ activity_id ]
												   completionHandler:^(NSArray<GKGameActivityDefinition *> *definitions, NSError *error) {
													   dispatch_async(dispatch_get_main_queue(), ^{
														   if (!definitions.firstObject) {
															   ERR_PRINT(vformat("Game Center has no game activity \"%s\": %s", ns_string(activity_id), ns_string(error.localizedDescription)));
															   return;
														   }
														   NSError *start_error = nil;
														   [(GKGameActivity *)party end];
														   party = [GKGameActivity startWithDefinition:definitions.firstObject partyCode:code error:&start_error];
														   if (!party) {
															   ERR_PRINT(vformat("Game Center couldn't start the party: %s", ns_string(start_error.localizedDescription)));
														   }
													   });
												   }];
	}
}

void GameCenter::end_party() {
	if (@available(iOS 26.0, macOS 26.0, tvOS 26.0, visionOS 26.0, *)) {
		[(GKGameActivity *)party end];
	}
	party = nil;
}

void GameCenter::_bind_methods() {
	ClassDB::bind_method(D_METHOD("authenticate"), &GameCenter::authenticate);
	ClassDB::bind_method(D_METHOD("is_authenticated"), &GameCenter::is_authenticated);
	ClassDB::bind_method(D_METHOD("get_player_name"), &GameCenter::get_player_name);
	ClassDB::bind_method(D_METHOD("get_team_player_id"), &GameCenter::get_team_player_id);
	ClassDB::bind_method(D_METHOD("fetch_identity_verification"), &GameCenter::fetch_identity_verification);
	ClassDB::bind_method(D_METHOD("report_achievement", "id", "percent"), &GameCenter::report_achievement, DEFVAL(100.0));
	ClassDB::bind_method(D_METHOD("submit_score", "leaderboard_id", "score"), &GameCenter::submit_score);
	ClassDB::bind_method(D_METHOD("get_achievements"), &GameCenter::get_achievements);
	ClassDB::bind_method(D_METHOD("get_scores", "leaderboard_id", "scope", "count"), &GameCenter::get_scores, DEFVAL(SCOPE_GLOBAL), DEFVAL(10));
	ClassDB::bind_method(D_METHOD("show_achievements"), &GameCenter::show_achievements);
	ClassDB::bind_method(D_METHOD("show_leaderboard", "leaderboard_id"), &GameCenter::show_leaderboard, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("invite_players", "player_group", "max_players"), &GameCenter::invite_players);
	ClassDB::bind_method(D_METHOD("start_party", "activity_id", "party_code"), &GameCenter::start_party);
	ClassDB::bind_method(D_METHOD("end_party"), &GameCenter::end_party);

	ADD_SIGNAL(MethodInfo("invite_accepted", PropertyInfo(Variant::INT, "player_group")));
	ADD_SIGNAL(MethodInfo("party_joined", PropertyInfo(Variant::STRING, "party_code")));

	BIND_ENUM_CONSTANT(SCOPE_GLOBAL);
	BIND_ENUM_CONSTANT(SCOPE_FRIENDS);
	BIND_ENUM_CONSTANT(SCOPE_AROUND_PLAYER);
}
