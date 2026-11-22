#pragma once

// Lobbies: playing together through Godot's high-level multiplayer. A lobby sets the scene tree's
// multiplayer peer: crossplay over WebRTC with the online service (trill/url), otherwise a Steam
// lobby. Ranked matchmaking, and the checks, flags, reports and blocks that keep play fair.

#include "core/object/class_db.h"
#include "core/object/object.h"

class Lobbies : public Object {
	GDCLASS(Lobbies, Object);

	static Lobbies *singleton;

protected:
	static void _bind_methods();

public:
	enum ReportReason {
		REPORT_CHEATING,
		REPORT_ABUSE,
		REPORT_NAME,
		REPORT_OTHER,
	};

	static Lobbies *get_singleton() { return singleton; }

	bool is_available() const;
	bool has_crossplay() const;
	bool can_invite() const;
	bool can_matchmake() const;

	Signal create();
	Signal join(const String &p_code);
	void leave();
	void kick(int64_t p_peer_id);
	void invite_friends();

	Signal matchmake(const String &p_mode, int p_players = 2);
	void cancel_matchmaking();
	void report_result(const Array &p_ranking);
	Signal get_rating(const String &p_mode);

	void check(const String &p_name, const Variant &p_value);
	void flag(int64_t p_peer_id, const String &p_reason);
	void report(int64_t p_peer_id, ReportReason p_reason, const String &p_details = String());
	void block(int64_t p_peer_id);

	Lobbies();
	~Lobbies();
};

VARIANT_ENUM_CAST(Lobbies::ReportReason);
