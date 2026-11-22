// Apple's headers first: GameController's (which GameKit brings in) use the name Key, which
// Godot also defines.
#include "game_center_internal.h"

#include "game_center_multiplayer_peer.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/object_id.h"

namespace {

// Each packet starts with its kind; a game packet then has its channel, its transfer mode and,
// for unreliable ordered packets, its sequence number.
enum Kind : uint8_t {
	KIND_GAME = 0,
	KIND_MESSAGE = 1,
};
const int GAME_HEADER = 7;

GameCenterMultiplayerPeer *peer_of(ObjectID p_id) {
	return Object::cast_to<GameCenterMultiplayerPeer>(ObjectDB::get_instance(p_id));
}

void found_on_main(ObjectID p_id, uint64_t p_match) {
	GKMatch *match = (__bridge_transfer GKMatch *)(void *)p_match;
	if (GameCenterMultiplayerPeer *peer = peer_of(p_id)) {
		peer->match_found((__bridge_retained void *)match);
	}
}

void not_found_on_main(ObjectID p_id, const String &p_error, const String &p_message) {
	if (GameCenterMultiplayerPeer *peer = peer_of(p_id)) {
		peer->match_not_found(p_error, p_message);
	}
}

void connected_on_main(ObjectID p_id, const String &p_player_id, bool p_connected) {
	if (GameCenterMultiplayerPeer *peer = peer_of(p_id)) {
		if (p_connected) {
			peer->player_connected(p_player_id);
		} else {
			peer->player_disconnected(p_player_id);
		}
	}
}

void message_on_main(ObjectID p_id, const String &p_player_id, const String &p_message) {
	if (GameCenterMultiplayerPeer *peer = peer_of(p_id)) {
		peer->message_arrived(p_player_id, p_message);
	}
}

String error_code(NSError *p_error) {
	if (p_error && [p_error.domain isEqualToString:GKErrorDomain]) {
		if (p_error.code == GKErrorCancelled) {
			return "cancelled";
		}
		if (p_error.code == GKErrorNotAuthenticated) {
			return "not_signed_in";
		}
	}
	return "failed";
}

// Hands a match (or why there's none) to the peer on Godot's main thread.
void deliver(ObjectID p_id, GKMatch *p_match, NSError *p_error) {
	if (p_match) {
		callable_mp_static(&found_on_main).call_deferred(p_id, (uint64_t)(__bridge_retained void *)p_match);
	} else {
		callable_mp_static(&not_found_on_main).call_deferred(p_id, error_code(p_error), game_center_string(p_error.localizedDescription));
	}
}

} // namespace

// The match's delegate and the invite screen's, passing everything to the peer by its ID (the
// peer may be gone by the time GameKit calls).
@interface GameCenterMatchDelegate : NSObject <GKMatchDelegate, GKMatchmakerViewControllerDelegate>
@property(nonatomic) ObjectID peer;
@end

@implementation GameCenterMatchDelegate

- (void)match:(GKMatch *)match didReceiveData:(NSData *)data fromRemotePlayer:(GKPlayer *)player {
	const uint8_t *bytes = (const uint8_t *)data.bytes;
	const NSUInteger size = data.length;
	if (size < 1) {
		return;
	}
	const String from = game_center_string(player.gamePlayerID);
	if (bytes[0] == KIND_MESSAGE) {
		String message;
		message.append_utf8((const char *)bytes + 1, size - 1);
		callable_mp_static(&message_on_main).call_deferred(self.peer, from, message);
		return;
	}
	if (bytes[0] != KIND_GAME || size < GAME_HEADER) {
		return;
	}
	GameCenterMultiplayerPeer::Packet packet;
	packet.from = from;
	packet.channel = bytes[1];
	packet.mode = (MultiplayerPeer::TransferMode)CLAMP((int)bytes[2], 0, 2);
	packet.sequence = (uint32_t)bytes[3] | ((uint32_t)bytes[4] << 8) | ((uint32_t)bytes[5] << 16) | ((uint32_t)bytes[6] << 24);
	packet.data.resize(size - GAME_HEADER);
	memcpy(packet.data.ptrw(), bytes + GAME_HEADER, size - GAME_HEADER);
	if (GameCenterMultiplayerPeer *peer = peer_of(self.peer)) {
		peer->packet_arrived(packet);
	}
}

- (void)match:(GKMatch *)match player:(GKPlayer *)player didChangeConnectionState:(GKPlayerConnectionState)state {
	callable_mp_static(&connected_on_main).call_deferred(self.peer, game_center_string(player.gamePlayerID), state == GKPlayerStateConnected);
}

- (void)match:(GKMatch *)match didFailWithError:(NSError *)error {
	ERR_PRINT(vformat("Game Center's match failed: %s", game_center_string(error.localizedDescription)));
}

- (BOOL)match:(GKMatch *)match shouldReinviteDisconnectedPlayer:(GKPlayer *)player {
	return NO;
}

- (void)matchmakerViewController:(GKMatchmakerViewController *)viewController didFindMatch:(GKMatch *)match {
	game_center_dismiss(viewController);
	deliver(self.peer, match, nil);
}

- (void)matchmakerViewControllerWasCancelled:(GKMatchmakerViewController *)viewController {
	game_center_dismiss(viewController);
}

- (void)matchmakerViewController:(GKMatchmakerViewController *)viewController didFailWithError:(NSError *)error {
	game_center_dismiss(viewController);
	ERR_PRINT(vformat("Game Center's invite screen failed: %s", game_center_string(error.localizedDescription)));
}

@end

namespace {

GameCenterMatchDelegate *delegate_for(GameCenterMultiplayerPeer *p_peer, void *&r_delegate) {
	if (!r_delegate) {
		GameCenterMatchDelegate *object = [GameCenterMatchDelegate new];
		object.peer = p_peer->get_instance_id();
		r_delegate = (__bridge_retained void *)object;
	}
	return (__bridge GameCenterMatchDelegate *)r_delegate;
}

} // namespace

int GameCenterMultiplayerPeer::peer_id_of(const String &p_player_id) {
	// FNV-1a over the ID, kept positive and clear of 0 (nobody) and 1 (the host).
	uint32_t hash = 2166136261u;
	const CharString utf8 = p_player_id.utf8();
	for (int i = 0; i < utf8.length(); i++) {
		hash ^= (uint8_t)utf8[i];
		hash *= 16777619u;
	}
	int id = (int)(hash & 0x7FFFFFFF);
	return id < 2 ? id + 2 : id;
}

GameCenterMultiplayerPeer::~GameCenterMultiplayerPeer() {
	forget_match();
	if (delegate) {
		CFRelease(delegate);
		delegate = nullptr;
	}
}

void GameCenterMultiplayerPeer::take_match(void *p_match) {
	if (p_match == match) {
		// Players added to this match (from the invite screen): they connect as usual.
		CFRelease(p_match);
		return;
	}
	forget_match();
	match = p_match;
	GKMatch *gk_match = (__bridge GKMatch *)match;
	gk_match.delegate = delegate_for(this, delegate);
	closed = false;
	for (GKPlayer *player in gk_match.players) {
		const String id = game_center_string(player.gamePlayerID);
		players.insert(id);
		to_announce.insert(id);
	}
}

void GameCenterMultiplayerPeer::forget_match() {
	if (!match) {
		return;
	}
	GKMatch *gk_match = (__bridge_transfer GKMatch *)match;
	gk_match.delegate = nil;
	[gk_match disconnect];
	match = nullptr;
}

void GameCenterMultiplayerPeer::match_found(void *p_match) {
	if (closed) {
		CFRelease(p_match);
		return;
	}
	take_match(p_match);
	emit_signal(SNAME("match_started"));
}

void GameCenterMultiplayerPeer::match_not_found(const String &p_error, const String &p_message) {
	emit_signal(SNAME("match_failed"), p_error, p_message);
}

void GameCenterMultiplayerPeer::player_connected(const String &p_player_id) {
	if (!match || ignored.has(p_player_id) || players.has(p_player_id)) {
		return;
	}
	players.insert(p_player_id);
	to_announce.insert(p_player_id);
	emit_signal(SNAME("player_connected"), p_player_id);
}

void GameCenterMultiplayerPeer::player_disconnected(const String &p_player_id) {
	if (!players.has(p_player_id)) {
		return;
	}
	players.erase(p_player_id);
	if (p_player_id == host && !is_server()) {
		host_left = true;
	}
	const bool announced = !to_announce.has(p_player_id);
	to_announce.erase(p_player_id);
	emit_signal(SNAME("player_disconnected"), p_player_id);
	if (announced && !ignored.has(p_player_id)) {
		emit_signal(SNAME("peer_disconnected"), get_peer_id_for_player(p_player_id));
	}
}

void GameCenterMultiplayerPeer::message_arrived(const String &p_player_id, const String &p_message) {
	if (players.has(p_player_id) && !ignored.has(p_player_id)) {
		emit_signal(SNAME("message_received"), p_player_id, p_message);
	}
}

void GameCenterMultiplayerPeer::packet_arrived(const Packet &p_packet) {
	MutexLock lock(mutex);
	arriving.push_back(p_packet);
}

Error GameCenterMultiplayerPeer::find_match(int p_min_players, int p_max_players, int64_t p_player_group) {
	ERR_FAIL_COND_V_MSG(!GKLocalPlayer.local.isAuthenticated, ERR_UNAUTHORIZED, "Sign in to Game Center before finding a match.");
	GKMatchRequest *request = [GKMatchRequest new];
	const int most = (int)[GKMatchRequest maxPlayersAllowedForMatchOfType:GKMatchTypePeerToPeer];
	request.minPlayers = CLAMP(p_min_players, 2, most);
	request.maxPlayers = CLAMP(p_max_players, (int)request.minPlayers, most);
	request.playerGroup = (NSUInteger)p_player_group;
	const ObjectID id = get_instance_id();
	closed = false;
	[GKMatchmaker.sharedMatchmaker findMatchForRequest:request
								 withCompletionHandler:^(GKMatch *found, NSError *error) {
									 deliver(id, found, error);
								 }];
	return OK;
}

// Players who start the same game activity with the same party code are matched together.
Error GameCenterMultiplayerPeer::find_party_match(const String &p_activity_id, const String &p_party_code) {
	ERR_FAIL_COND_V_MSG(!GKLocalPlayer.local.isAuthenticated, ERR_UNAUTHORIZED, "Sign in to Game Center before finding a match.");
	if (@available(iOS 26.0, macOS 26.0, tvOS 26.0, visionOS 26.0, *)) {
		NSString *code = game_center_ns_string(p_party_code);
		ERR_FAIL_COND_V_MSG(![GKGameActivity isValidPartyCode:code], ERR_INVALID_PARAMETER, vformat("\"%s\" isn't a Game Center party code.", p_party_code));
		const ObjectID id = get_instance_id();
		closed = false;
		[GKGameActivityDefinition loadGameActivityDefinitionsWithIDs:@[ game_center_ns_string(p_activity_id) ]
												   completionHandler:^(NSArray<GKGameActivityDefinition *> *definitions, NSError *error) {
													   dispatch_async(dispatch_get_main_queue(), ^{
														   if (!definitions.firstObject) {
															   deliver(id, nil, error);
															   return;
														   }
														   NSError *start_error = nil;
														   GKGameActivity *activity = [GKGameActivity startWithDefinition:definitions.firstObject partyCode:code error:&start_error];
														   if (!activity) {
															   deliver(id, nil, start_error);
															   return;
														   }
														   game_center_set_party(activity);
														   [activity findMatchWithCompletionHandler:^(GKMatch *found, NSError *match_error) {
															   deliver(id, found, match_error);
														   }];
													   });
												   }];
		return OK;
	}
	return ERR_UNAVAILABLE;
}

Error GameCenterMultiplayerPeer::accept_invite() {
	GKInvite *invite = game_center_take_invite();
	ERR_FAIL_NULL_V_MSG(invite, ERR_DOES_NOT_EXIST, "There's no accepted Game Center invite to take.");
	const ObjectID id = get_instance_id();
	closed = false;
	[GKMatchmaker.sharedMatchmaker matchForInvite:invite
								completionHandler:^(GKMatch *found, NSError *error) {
									deliver(id, found, error);
								}];
	return OK;
}

// Apple's invite screen: for a new match, or for adding players to this one.
Error GameCenterMultiplayerPeer::invite(int p_max_players) {
	ERR_FAIL_COND_V_MSG(!GKLocalPlayer.local.isAuthenticated, ERR_UNAUTHORIZED, "Sign in to Game Center before inviting players.");
	GKMatchRequest *request = [GKMatchRequest new];
	request.minPlayers = 2;
	request.maxPlayers = CLAMP(p_max_players, 2, (int)[GKMatchRequest maxPlayersAllowedForMatchOfType:GKMatchTypePeerToPeer]);
	GKMatchmakerViewController *controller = [[GKMatchmakerViewController alloc] initWithMatchRequest:request];
	ERR_FAIL_NULL_V_MSG(controller, ERR_CANT_CREATE, "Game Center couldn't open its invite screen.");
	controller.matchmakerDelegate = delegate_for(this, delegate);
	controller.canStartWithMinimumPlayers = YES;
	if (match) {
		[controller addPlayersToMatch:(__bridge GKMatch *)match];
	}
	closed = false;
	game_center_present(controller);
	return OK;
}

// One friend (from GameCenter.load_friends()) invited without the invite screen.
Error GameCenterMultiplayerPeer::invite_player(const String &p_player_id, int p_max_players) {
	GKPlayer *player = game_center_friend(p_player_id);
	ERR_FAIL_NULL_V_MSG(player, ERR_DOES_NOT_EXIST, "Load the player's friends (GameCenter.load_friends) before inviting one.");
	GKMatchRequest *request = [GKMatchRequest new];
	request.minPlayers = 2;
	request.maxPlayers = CLAMP(p_max_players, 2, (int)[GKMatchRequest maxPlayersAllowedForMatchOfType:GKMatchTypePeerToPeer]);
	request.recipients = @[ player ];
	const ObjectID id = get_instance_id();
	closed = false;
	if (match) {
		[GKMatchmaker.sharedMatchmaker addPlayersToMatch:(__bridge GKMatch *)match
											matchRequest:request
									   completionHandler:^(NSError *error) {
										   if (error) {
											   ERR_PRINT(vformat("Game Center couldn't invite the player: %s", game_center_string(error.localizedDescription)));
										   }
									   }];
	} else {
		[GKMatchmaker.sharedMatchmaker findMatchForRequest:request
									 withCompletionHandler:^(GKMatch *found, NSError *error) {
										 deliver(id, found, error);
									 }];
	}
	return OK;
}

void GameCenterMultiplayerPeer::cancel_matchmaking() {
	[GKMatchmaker.sharedMatchmaker cancel];
}

void GameCenterMultiplayerPeer::set_host(const String &p_player_id) {
	host = p_player_id;
	host_left = false;
	// Every peer ID changes for the game: it hears about everyone again.
	for (const String &player : players) {
		to_announce.insert(player);
	}
}

String GameCenterMultiplayerPeer::get_local_player_id() const {
	return GKLocalPlayer.local.isAuthenticated ? game_center_string(GKLocalPlayer.local.gamePlayerID) : String();
}

PackedStringArray GameCenterMultiplayerPeer::get_player_ids() const {
	PackedStringArray list;
	for (const String &player : players) {
		if (!ignored.has(player)) {
			list.push_back(player);
		}
	}
	list.sort();
	return list;
}

int GameCenterMultiplayerPeer::get_peer_id_for_player(const String &p_player_id) const {
	return p_player_id == host ? 1 : peer_id_of(p_player_id);
}

String GameCenterMultiplayerPeer::get_player_for_peer_id(int p_peer_id) const {
	if (p_peer_id == get_unique_id()) {
		return get_local_player_id();
	}
	for (const String &player : players) {
		if (get_peer_id_for_player(player) == p_peer_id) {
			return player;
		}
	}
	return String();
}

Vector<String> GameCenterMultiplayerPeer::targets() const {
	Vector<String> list;
	for (const String &player : players) {
		if (ignored.has(player) || to_announce.has(player)) {
			continue;
		}
		const int peer_id = get_peer_id_for_player(player);
		if (target_peer == 0 || target_peer == peer_id || (target_peer < 0 && -target_peer != peer_id)) {
			list.push_back(player);
		}
	}
	return list;
}

Error GameCenterMultiplayerPeer::send_message(const String &p_message) {
	ERR_FAIL_NULL_V(match, ERR_UNCONFIGURED);
	const CharString utf8 = p_message.utf8();
	NSMutableData *data = [NSMutableData dataWithLength:utf8.length() + 1];
	uint8_t *bytes = (uint8_t *)data.mutableBytes;
	bytes[0] = KIND_MESSAGE;
	memcpy(bytes + 1, utf8.get_data(), utf8.length());
	NSError *error = nil;
	if (![(__bridge GKMatch *)match sendDataToAllPlayers:data withDataMode:GKMatchSendDataReliable error:&error]) {
		ERR_PRINT(vformat("Game Center couldn't send a message: %s", game_center_string(error.localizedDescription)));
		return FAILED;
	}
	return OK;
}

int GameCenterMultiplayerPeer::get_available_packet_count() const {
	return packets.size();
}

Error GameCenterMultiplayerPeer::get_packet(const uint8_t **r_buffer, int &r_buffer_size) {
	ERR_FAIL_COND_V(packets.is_empty(), ERR_UNAVAILABLE);
	current = packets.front()->get();
	packets.pop_front();
	*r_buffer = current.data.ptr();
	r_buffer_size = current.data.size();
	return OK;
}

Error GameCenterMultiplayerPeer::put_packet(const uint8_t *p_buffer, int p_buffer_size) {
	ERR_FAIL_NULL_V_MSG(match, ERR_UNCONFIGURED, "The Game Center match hasn't started.");
	ERR_FAIL_COND_V(p_buffer_size > MAX_PACKET_SIZE - GAME_HEADER, ERR_OUT_OF_MEMORY);
	const Vector<String> recipients = targets();
	if (recipients.is_empty()) {
		return OK;
	}
	const TransferMode mode = get_transfer_mode();
	const uint32_t sequence = mode == TRANSFER_MODE_UNRELIABLE_ORDERED ? next_sequence++ : 0;
	NSMutableData *data = [NSMutableData dataWithLength:p_buffer_size + GAME_HEADER];
	uint8_t *bytes = (uint8_t *)data.mutableBytes;
	bytes[0] = KIND_GAME;
	bytes[1] = (uint8_t)CLAMP(get_transfer_channel(), 0, 255);
	bytes[2] = (uint8_t)mode;
	bytes[3] = sequence & 0xFF;
	bytes[4] = (sequence >> 8) & 0xFF;
	bytes[5] = (sequence >> 16) & 0xFF;
	bytes[6] = (sequence >> 24) & 0xFF;
	memcpy(bytes + GAME_HEADER, p_buffer, p_buffer_size);
	GKMatch *gk_match = (__bridge GKMatch *)match;
	NSMutableArray<GKPlayer *> *to = [NSMutableArray array];
	for (GKPlayer *player in gk_match.players) {
		if (recipients.has(game_center_string(player.gamePlayerID))) {
			[to addObject:player];
		}
	}
	NSError *error = nil;
	const GKMatchSendDataMode send_mode = mode == TRANSFER_MODE_RELIABLE ? GKMatchSendDataReliable : GKMatchSendDataUnreliable;
	if (![gk_match sendData:data toPlayers:to dataMode:send_mode error:&error]) {
		return FAILED;
	}
	return OK;
}

int GameCenterMultiplayerPeer::get_packet_peer() const {
	ERR_FAIL_COND_V(packets.is_empty(), 0);
	return get_peer_id_for_player(packets.front()->get().from);
}

MultiplayerPeer::TransferMode GameCenterMultiplayerPeer::get_packet_mode() const {
	ERR_FAIL_COND_V(packets.is_empty(), TRANSFER_MODE_RELIABLE);
	return packets.front()->get().mode;
}

int GameCenterMultiplayerPeer::get_packet_channel() const {
	ERR_FAIL_COND_V(packets.is_empty(), 0);
	return packets.front()->get().channel;
}

// A match can't remove a player: this game stops playing with them instead.
void GameCenterMultiplayerPeer::disconnect_peer(int p_peer, bool p_force) {
	const String player = get_player_for_peer_id(p_peer);
	if (player.is_empty() || ignored.has(player)) {
		return;
	}
	ignored.insert(player);
	const bool announced = !to_announce.has(player);
	to_announce.erase(player);
	if (announced) {
		emit_signal(SNAME("peer_disconnected"), p_peer);
	}
}

bool GameCenterMultiplayerPeer::is_server() const {
	return !host.is_empty() && host == get_local_player_id();
}

void GameCenterMultiplayerPeer::poll() {
	if (!match) {
		return;
	}
	// A player's packets reach the game only once it knows about them.
	for (const String &player : to_announce) {
		if (!host.is_empty() && !ignored.has(player)) {
			emit_signal(SNAME("peer_connected"), get_peer_id_for_player(player));
		}
	}
	if (!host.is_empty()) {
		to_announce.clear();
	}
	List<Packet> batch;
	{
		MutexLock lock(mutex);
		batch = arriving;
		arriving.clear();
	}
	for (const Packet &packet : batch) {
		if (!players.has(packet.from) || ignored.has(packet.from) || to_announce.has(packet.from)) {
			continue;
		}
		if (packet.mode == TRANSFER_MODE_UNRELIABLE_ORDERED) {
			const String key = packet.from + " " + itos(packet.channel);
			const HashMap<String, uint32_t>::Iterator latest = latest_sequence.find(key);
			if (latest && (int32_t)(packet.sequence - latest->value) <= 0) {
				continue;
			}
			latest_sequence[key] = packet.sequence;
		}
		packets.push_back(packet);
	}
}

void GameCenterMultiplayerPeer::close() {
	closed = true;
	forget_match();
	players.clear();
	to_announce.clear();
	ignored.clear();
	packets.clear();
	latest_sequence.clear();
	host = String();
	host_left = false;
	MutexLock lock(mutex);
	arriving.clear();
}

int GameCenterMultiplayerPeer::get_unique_id() const {
	return is_server() ? 1 : peer_id_of(get_local_player_id());
}

MultiplayerPeer::ConnectionStatus GameCenterMultiplayerPeer::get_connection_status() const {
	if (closed) {
		return CONNECTION_DISCONNECTED;
	}
	// A host is connected from the start, before anyone joins, as with Godot's other peers.
	if (is_server()) {
		return CONNECTION_CONNECTED;
	}
	if (!match) {
		return CONNECTION_CONNECTING;
	}
	if (host_left) {
		return CONNECTION_DISCONNECTED;
	}
	// Anyone else is connected once they're connected to the host.
	return players.has(host) ? CONNECTION_CONNECTED : CONNECTION_CONNECTING;
}

void GameCenterMultiplayerPeer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("find_match", "min_players", "max_players", "player_group"), &GameCenterMultiplayerPeer::find_match, DEFVAL(0));
	ClassDB::bind_method(D_METHOD("find_party_match", "activity_id", "party_code"), &GameCenterMultiplayerPeer::find_party_match);
	ClassDB::bind_method(D_METHOD("accept_invite"), &GameCenterMultiplayerPeer::accept_invite);
	ClassDB::bind_method(D_METHOD("invite", "max_players"), &GameCenterMultiplayerPeer::invite);
	ClassDB::bind_method(D_METHOD("invite_player", "player_id", "max_players"), &GameCenterMultiplayerPeer::invite_player);
	ClassDB::bind_method(D_METHOD("cancel_matchmaking"), &GameCenterMultiplayerPeer::cancel_matchmaking);
	ClassDB::bind_method(D_METHOD("has_match"), &GameCenterMultiplayerPeer::has_match);
	ClassDB::bind_method(D_METHOD("set_host", "player_id"), &GameCenterMultiplayerPeer::set_host);
	ClassDB::bind_method(D_METHOD("get_host"), &GameCenterMultiplayerPeer::get_host);
	ClassDB::bind_method(D_METHOD("get_local_player_id"), &GameCenterMultiplayerPeer::get_local_player_id);
	ClassDB::bind_method(D_METHOD("get_player_ids"), &GameCenterMultiplayerPeer::get_player_ids);
	ClassDB::bind_method(D_METHOD("get_peer_id_for_player", "player_id"), &GameCenterMultiplayerPeer::get_peer_id_for_player);
	ClassDB::bind_method(D_METHOD("get_player_for_peer_id", "peer_id"), &GameCenterMultiplayerPeer::get_player_for_peer_id);
	ClassDB::bind_method(D_METHOD("send_message", "message"), &GameCenterMultiplayerPeer::send_message);

	ADD_SIGNAL(MethodInfo("match_started"));
	ADD_SIGNAL(MethodInfo("match_failed", PropertyInfo(Variant::STRING, "error"), PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("player_connected", PropertyInfo(Variant::STRING, "player_id")));
	ADD_SIGNAL(MethodInfo("player_disconnected", PropertyInfo(Variant::STRING, "player_id")));
	ADD_SIGNAL(MethodInfo("message_received", PropertyInfo(Variant::STRING, "player_id"), PropertyInfo(Variant::STRING, "message")));
}
