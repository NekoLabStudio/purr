#pragma once

// Achievements, for Purr. Every player's achievements are kept in their saves (the
// purr_achievements slot: {id: progress from 0 to 1}), so they follow the saves: to the account
// once the player adds an email, and from there to the next platform they play on. Player 1's
// also go to the platform (Steam, Game Center or Play Games): once signed in, Purr loads what
// the platform has, adds anything new to the slot, and sends the platform only what it lacks.

#include "player_saves.h"

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"

class PurrAchievements : public RefCounted {
	GDCLASS(PurrAchievements, RefCounted);

	String platform; // "steam", "game_center", "play_games", or empty without one.
	bool loaded = false; // What the platform has is known: sending can start.
	Dictionary on_platform; // Platform name -> progress the platform has.
	Dictionary total_steps; // Play Games: platform name -> steps of an incremental achievement.
	HashMap<String, String> ids; // Platform name -> Purr ID, where Project Settings map them.

	static Ref<PlayerSaves> player_saves();
	String platform_name(const String &p_id) const;
	String purr_id(const String &p_platform_name) const;
	void load_steam();
	void steam_stats_received(int64_t p_game_id, int64_t p_result, int64_t p_user_id);
	void platform_loaded(const Dictionary &p_result);
	void send(const String &p_id, double p_progress);

protected:
	static void _bind_methods() {}

public:
	// For any player's saves: records progress (kept only if further than before), and reads
	// the list. record() returns whether it changed.
	static bool record(const Ref<PlayerSaves> &p_saves, const String &p_id, double p_progress);
	static Dictionary read(const Ref<PlayerSaves> &p_saves);

	// Player 1 signed in: loads what their platform has.
	void start();
	// Player 1's progress, from current of total steps (unlocked at total).
	void report(const String &p_id, int64_t p_current, int64_t p_total);
	// Sends the platform whatever the slot has that it lacks (after the slot synced).
	void send_missing();
	void show();
};
