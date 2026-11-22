#pragma once

// A lobby without Purr's online services, on the platform's own multiplayer: a Steam lobby
// (PurrSteamLobby) or a Game Center match (PurrGameCenterLobby). The game plays through Godot's
// high-level multiplayer as in a crossplay lobby, with the host as peer 1. What the service would
// decide (removals, checks, flags, ranked results) every player's game decides together instead,
// through a PurrLobbyReferee fed with the platform's messages. Ratings and blocked players are
// kept in the player's saves (PURR_MULTIPLAYER_SLOT), synced through the platform's cloud.
// Internal.

#include "purr_lobby_referee.h"

#include "core/object/ref_counted.h"

class PurrPlatformLobby : public RefCounted {
	GDCLASS(PurrPlatformLobby, RefCounted);

	void referee_check_completed(const String &p_name, bool p_agreed, const Array &p_disagreeing);
	void referee_player_flagged(int64_t p_peer_id, const String &p_reason);
	void referee_result_agreed(const Array &p_ranking, const PackedFloat64Array &p_changes);
	void referee_member_kicked(const String &p_member);

protected:
	static void _bind_methods() {}

	Ref<PurrLobbyReferee> referee;
	String code;
	String mode; // A matchmade lobby's ranked mode.
	Ref<RefCounted> pending_call; // create(), join() or matchmake(), until the lobby is ready.
	bool active = false;

	// What the platform provides.
	virtual String _platform() const = 0; // Blocked players are kept by platform ("steam", ...).
	virtual String _own_member() const = 0; // This player's ID on the platform.
	virtual void _send(const Dictionary &p_message) = 0; // To everyone, this player included.
	virtual void _drop(const String &p_member) = 0; // Stops playing with a removed player.
	virtual void _close() = 0; // Leaves the platform's lobby and clears the multiplayer peer.
	virtual void _report(const String &p_member) {}

	// What the platform calls as things happen.
	void entered(const String &p_code); // In the lobby: completes the waiting call and emits joined.
	void failed(const String &p_error, const String &p_message);
	void member_joined(const String &p_member, int64_t p_peer_id);
	void member_left(const String &p_member);
	void received(const String &p_from, const Dictionary &p_message);
	void host_is(const String &p_member, bool p_changed);
	// A matchmade lobby's match begins: results count for this mode, and this player's rating
	// goes to the others for working out the changes.
	void start_ranked(const String &p_mode);
	// Whether a member is one this player blocked, or one the host must turn away.
	bool turned_away(const String &p_member) const;

public:
	// Each completes the call with {"ok", "code"} or {"ok": false, "error", "message"}.
	virtual void create(const Ref<RefCounted> &p_call) = 0;
	virtual void join(const String &p_code, const Ref<RefCounted> &p_call) = 0;
	virtual void matchmake(const String &p_mode, int p_players, const Ref<RefCounted> &p_call) = 0;
	virtual void cancel_matchmaking() = 0;
	virtual bool is_matchmaking() const = 0;

	void leave();
	bool is_active() const { return active; }
	bool is_host() const;
	String get_code() const { return code; }

	void kick(int64_t p_peer_id);
	void check(const String &p_name, const Variant &p_value);
	void flag(int64_t p_peer_id, const String &p_reason);
	void report_result(const Array &p_ranking);
	void block(int64_t p_peer_id);
	void report(int64_t p_peer_id);

	// The player's rating in a ranked mode, from their saves: {"ok", "data": {"rating", "matches"}}.
	static Dictionary get_rating(const String &p_mode);
	static double own_rating(const String &p_mode);
	static bool is_blocked(const String &p_platform, const String &p_member);

	PurrPlatformLobby();
};
