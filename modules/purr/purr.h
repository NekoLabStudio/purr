#pragma once

// Purr: the engine side of what every game needs (signing in, saves, achievements, leaderboards,
// lobbies, friends, local players and the store), the same on every platform, behind the singletons
// games use (Account, LocalPlayers, Saves, Achievements, Leaderboards, Lobbies, Friends, Store). Underneath it
// picks the platform (Steam where it's running, Game Center, Play Games, otherwise a device
// account) and Purr's online services when online/url is set; without them, each platform
// directly, lobbies included (PurrPlatformLobby). Internal: games never see this class.

#include "purr_achievements.h"
#include "purr_friends.h"
#include "purr_leaderboards.h"
#include "purr_game_center_lobby.h"
#include "purr_lobby.h"
#include "purr_steam_lobby.h"
#include "purr_store.h"
#include "local_player.h"

#include "core/object/object.h"
#include "core/templates/hash_map.h"

// Fires a call object's completed signal on the next idle frame (shared with LocalPlayer).
void purr_complete(const Ref<RefCounted> &p_call, const Variant &p_result);
// A failed call's result: {"ok": false, "error", "message"}.
Dictionary purr_failure(const String &p_error, const String &p_message);

class Purr : public Object {
	GDCLASS(Purr, Object);

	static Purr *singleton;

	Ref<LocalPlayer> player;
	Array local_players;
	Array remembered_guests;
	String provider = "local";
	String device_label;
	Ref<RefCounted> sign_in_call; // The sign-in under way: a second sign_in() meanwhile gets its result.
	bool email_added = false; // Player 1's account has an email, so its saves are on the service.
	bool name_chosen = false; // Player 1 chose their name (it wins over a platform's).
	// The address a code was last sent to, its challenge, and the current address if changing.
	String pending_email;
	String pending_challenge;
	String pending_current_email;
	String pending_guest_email;
	String pending_guest_challenge;

	// Crossplay lobbies, through the online service.
	Ref<PurrLobby> lobby;
	void steam_join_game_requested(int64_t p_friend_id, const String &p_connect);

	// Friends, and invites accepted before signing in finished (joined once it has).
	Ref<PurrFriends> friends;
	String invited_code;
	String invited_match_sender; // A Game Center match invite's sender.
	bool command_line_read = false; // An invite on the command line is joined once, not at every sign-in.
	void join_steam_lobby(uint64_t p_lobby_id);

	// Lobbies on the platform's own multiplayer, without the online service.
	Ref<PurrPlatformLobby> platform_lobby_instance;
	Ref<PurrPlatformLobby> platform_lobby();
	Ref<PurrPlatformLobby> active_platform_lobby() const;
	void leave_platform_lobby();
	void game_center_match_invite_accepted(const String &p_sender_id);

	Ref<PurrAchievements> achievements;
	Ref<PurrLeaderboards> leaderboards;
	Ref<PurrStore> store_items;
	void store_done(const Dictionary &p_result, const Ref<RefCounted> &p_call);

	void game_center_authenticated(const Dictionary &p_result, const Ref<RefCounted> &p_call);
	void game_center_verified(const Dictionary &p_result, const Ref<RefCounted> &p_call);
	void play_games_authenticated(const Dictionary &p_result, const Ref<RefCounted> &p_call);
	void play_games_code_received(const Dictionary &p_result, const Ref<RefCounted> &p_call);
	String platform_name() const;
	String platform_owner() const;
	void carry_over_saves();
	void sign_in_without_platform(const Ref<RefCounted> &p_call);
	void sign_in_proven(const Dictionary &p_proof, const Ref<RefCounted> &p_call);
	void sign_in_platform(const Ref<RefCounted> &p_call);
	void email_proven(const Dictionary &p_proof, const String &p_code, const Ref<RefCounted> &p_call);
	PlayerSaves::Backend save_backend(bool p_online) const;
	void attach_saves();

	void sign_in_steam_online(const Ref<RefCounted> &p_call);
	void steam_ticket(int64_t p_auth_ticket, int64_t p_result, int64_t p_size, const PackedByteArray &p_ticket, const Ref<RefCounted> &p_call);
	void online_signed_in(const Dictionary &p_result, const Ref<RefCounted> &p_call);
	void finish_sign_in(bool p_online, const String &p_account_id, const String &p_name, const Ref<RefCounted> &p_call);
	void save_synced(const String &p_slot);
	void save_conflicted(const String &p_slot, const Dictionary &p_local_summary, const Dictionary &p_remote_summary);
	void online_update_required(const Variant &p_min_version, const Variant &p_max_version);
	void online_service_outdated();
	void online_not_owned();
	void display_name_saved(const Dictionary &p_result, const Ref<RefCounted> &p_call);
	void load_remembered();

	void steam_join_requested(int64_t p_lobby_id, int64_t p_friend_id);

	void email_code_sent(const Dictionary &p_result, const Ref<RefCounted> &p_call, bool p_guest);
	void email_verified(const Dictionary &p_result, const Ref<RefCounted> &p_call);
	void guest_email_verified(const Dictionary &p_result, const Ref<RefCounted> &p_call, bool p_remember);

protected:
	static void _bind_methods();

public:
	// The longest name a player can choose (the online service's limit too).
	static constexpr int MAX_NAME_LENGTH = 24;

	static Purr *get_singleton() { return singleton; }

	Signal sign_in();
	bool is_signed_in() const { return player.is_valid(); }
	String get_player_name() const;
	String get_provider() const { return provider; }
	bool is_available(const String &p_feature) const;


	void unlock_achievement(const String &p_id);
	void set_achievement_progress(const String &p_id, int64_t p_current, int64_t p_total);
	Dictionary get_achievements() const;
	void show_achievements();
	void submit_score(const String &p_leaderboard, int64_t p_score);
	Signal get_scores(const String &p_leaderboard, const String &p_scope = "global", int p_count = 10, bool p_previous = false);
	void show_leaderboard(const String &p_leaderboard = String());
	Ref<PurrStore> store() const { return store_items; }
	Signal store_purchase(const String &p_id);
	Signal store_restore();

	Signal lobby_create();
	Signal lobby_join(const String &p_code);
	void lobby_leave();
	void invite_friends();
	void invite_friend(const String &p_friend_id);
	Array get_friends() const;
	Array get_friend_service_ids() const;
	void lobby_kick(int64_t p_peer_id);
	void report_player(int64_t p_peer_id, const String &p_reason, const String &p_details = String());
	void block_player(int64_t p_peer_id);

	Signal matchmake(const String &p_mode, int p_players = 2);
	void matchmake_cancel();
	void report_result(const Array &p_ranking);
	Signal get_rating(const String &p_mode);
	Signal get_service_version();
	bool needs_display_name() const;
	Signal set_display_name(const String &p_name);
	void check(const String &p_name, const Variant &p_value);
	void flag(int64_t p_peer_id, const String &p_reason);

	Ref<LocalPlayer> get_player() const { return player; }
	Array get_local_players() const { return local_players; }
	Array get_remembered_guests() const { return remembered_guests; }
	Ref<LocalPlayer> add_guest(const Ref<LocalPlayer> &p_remembered = Ref<LocalPlayer>());
	Signal guest_email_start(const String &p_email);
	Signal guest_email_finish(const String &p_code, bool p_remember = false, const String &p_name = String());

	Signal email_link_start(const String &p_email, const String &p_current_email = String());
	Signal email_link_finish(const String &p_code);
	bool has_email() const { return email_added; }
	void remove_guest(const Ref<LocalPlayer> &p_guest);

	Signal get_devices();
	Signal get_activity();
	Signal forget_device(const String &p_device_id);
	Signal delete_account();
	void account_deleted(const Dictionary &p_result, const Ref<RefCounted> &p_call);

	String get_device_label() const;
	void set_device_label(const String &p_label) { device_label = p_label; }

	// For LocalPlayer, PlayerSaves, PurrLobby, PurrFriends, PurrAchievements and PurrLeaderboards.
	Signal deferred_result(const Variant &p_result);
	Object *steam() const;
	bool steam_ready() const;
	// Starts Steam if the game was started from it and nothing has yet; returns steam_ready().
	bool start_steam();
	int64_t steam_constant(const String &p_name) const;
	Object *game_center() const;
	bool game_center_ready() const;
	Object *play_games() const;
	bool play_games_ready() const;
	bool online_lobbies() const;
	// The lobby the player is now in (empty once they've left), for friends to see.
	void set_lobby_presence(const String &p_code, bool p_crossplay);
	// Joins a crossplay lobby the player accepted an invite to, once signed in.
	void join_invited(const String &p_code);
	// Where saves go in the platform's cloud when it isn't Steam Cloud: ICloud or PlayGames, both
	// with the same file calls; null if neither is ready.
	Object *cloud_store() const;
	void store_remembered(const Ref<LocalPlayer> &p_guest);
	void forget_remembered(const Ref<LocalPlayer> &p_guest);
	PlayerSaves::Backend local_profile_backend() const;

	Purr();
	~Purr();
};
