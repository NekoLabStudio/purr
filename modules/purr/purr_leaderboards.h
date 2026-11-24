#pragma once

// Leaderboards, for Purr. With the online services, scores go to them, one board across every
// platform, and come back from them for the game's own screens; each one also goes to the
// platform's board, so its own screens keep working. Without them, the platform's leaderboards
// (Steam, Game Center or Play Games) are the only ones. Each leaderboard's name on a platform is
// leaderboards/<id>/<platform> in Project Settings, or the same ID; its order, reset and limits on
// the online services are leaderboards/<id>/order, reset, min and max.

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"

class PurrLeaderboards : public RefCounted {
	GDCLASS(PurrLeaderboards, RefCounted);

	// Steam: leaderboards are found by name before use, one at a time (its answer doesn't say
	// which), then remembered; what waits for one runs once it's found.
	HashMap<String, uint64_t> steam_handles;
	Vector<String> steam_to_find;
	HashMap<String, Vector<Callable>> steam_waiting;
	// Score downloads under way, by leaderboard, oldest first, and the call each one answers.
	struct SteamDownload {
		Ref<RefCounted> call;
		String scope;
		int count = 0;
	};
	HashMap<uint64_t, Vector<SteamDownload>> steam_downloads;

	String platform() const;
	Dictionary board_settings(const String &p_id) const;
	static void online_scores(const Dictionary &p_result, const Ref<RefCounted> &p_call);
	static void online_submitted(const Dictionary &p_result, const String &p_id);
	String platform_name(const String &p_id, const String &p_platform) const;
	void with_steam_leaderboard(const String &p_name, const Callable &p_then);
	void find_next_steam_leaderboard();
	void steam_leaderboard_found(int64_t p_handle, int64_t p_found);
	void steam_upload(uint64_t p_handle, int64_t p_score);
	void steam_download(uint64_t p_handle, const String &p_scope, int p_count, const Ref<RefCounted> &p_call);
	void steam_scores_downloaded(const String &p_message, int64_t p_handle, const Array &p_entries);
	void steam_show(uint64_t p_handle);

protected:
	static void _bind_methods() {}

public:
	void submit(const String &p_id, int64_t p_score);
	// Completes the call with {"ok", "scores": [{"rank", "name", "score", "player"}]}; the
	// previous period (the online services' boards that reset) with p_previous.
	void load_scores(const String &p_id, const String &p_scope, int p_count, bool p_previous, const Ref<RefCounted> &p_call);
	void show(const String &p_id);
};
