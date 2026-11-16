#pragma once

// Leaderboards: scores on Steam, Game Center or Play Games, with each leaderboard's platform names
// in Project Settings (leaderboards/<id>/<platform>).

#include "core/object/class_db.h"
#include "core/object/object.h"

class Leaderboards : public Object {
	GDCLASS(Leaderboards, Object);

	static Leaderboards *singleton;

protected:
	static void _bind_methods();

public:
	enum Scope {
		SCOPE_GLOBAL, // The best scores.
		SCOPE_FRIENDS, // The best among the player's friends.
		SCOPE_AROUND_PLAYER, // Those around the player's own (the best if they have none).
	};

	static Leaderboards *get_singleton() { return singleton; }

	bool is_available() const;
	void submit_score(const String &p_id, int64_t p_score);
	Signal get_scores(const String &p_id, Scope p_scope = SCOPE_GLOBAL, int p_count = 10);
	void show(const String &p_id = String());

	Leaderboards();
	~Leaderboards();
};

VARIANT_ENUM_CAST(Leaderboards::Scope);
