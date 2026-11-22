#pragma once

// Godot's side of LobbyRules for PurrPlatformLobby: reads the platform's messages (Dictionaries,
// as sent by the make_*() helpers), turns members into the peer IDs the game sees, and emits
// what the rules decide as signals. Internal.

#include "lobby_rules.h"

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"

class PurrLobbyReferee : public RefCounted {
	GDCLASS(PurrLobbyReferee, RefCounted);

	LobbyRules rules;
	HashMap<String, int64_t> peer_ids;

	void emit_events(const std::vector<LobbyRules::Event> &p_events);

protected:
	static void _bind_methods();

public:
	static constexpr double START_RATING = LobbyRules::START_RATING;

	// Forgets everything, for the next lobby.
	void reset();
	void add_member(const String &p_member, int64_t p_peer_id);
	void remove_member(const String &p_member);
	void set_peer_id(const String &p_member, int64_t p_peer_id);
	String get_member(int64_t p_peer_id) const;
	int64_t get_peer_id(const String &p_member) const;

	// The host is the only one whose removals count.
	void set_host(const String &p_member);
	String get_host() const;
	// A matchmade lobby: results change ratings once enough players agree.
	void set_ranked(bool p_ranked) { rules.set_ranked(p_ranked); }
	bool is_kicked(const String &p_member) const;

	// A message from a member (themselves included), as the platform delivered it.
	void receive(const String &p_from, const Dictionary &p_message, uint64_t p_now_msec);

	// The messages a player's game sends.
	static Dictionary make_check(const String &p_name, const String &p_value);
	static Dictionary make_flag(const String &p_target, const String &p_reason);
	static Dictionary make_result(const PackedStringArray &p_ranking);
	static Dictionary make_rating(double p_rating);
	static Dictionary make_kick(const String &p_member);
	static bool is_valid_check_name(const String &p_name);
	static bool is_valid_flag_reason(const String &p_reason);
};
