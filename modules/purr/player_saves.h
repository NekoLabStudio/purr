#pragma once

// One player's saves. Local first: a save is written to disk at once and the game never waits on
// the network; it's then synced in the background with the player's backend:
// - "online": the online service, once the player has added an email. Each upload names the
//   version it replaces; if another device saved since, the conflict signal offers both and the
//   game chooses.
// - "platform": Steam Cloud, which syncs the files written through it by itself; or a cloud
//   store (iCloud through CloudKit on Apple platforms, Play Games' saved games on Android), where
//   each save is uploaded after it's written, and a save changed on both sides raises the
//   conflict signal.
// - "local": this device only.
// On this device (PurrSaveStore): <slot>.save, and <slot>.json with the version last synced,
// whether it has changes not yet synced, and the game's summary of it.

#include "purr_save_store.h"

#include "core/object/class_db.h"
#include "core/object/ref_counted.h"
#include "core/templates/hash_set.h"
#include "core/variant/dictionary.h"

// The slot where a player's achievements are kept, each with its progress from 0 to 1 (1 is
// unlocked), so the platforms they play on next (or, for a guest, their own) can unlock them too.
// It never raises the conflict signal: the two sides' lists are merged.
inline constexpr const char *PURR_ACHIEVEMENTS_SLOT = "purr_achievements";

class PlayerSaves : public RefCounted {
	GDCLASS(PlayerSaves, RefCounted);

public:
	enum Backend {
		BACKEND_LOCAL,
		BACKEND_PLATFORM,
		BACKEND_ONLINE,
	};

private:
	String owner;
	Backend backend = BACKEND_LOCAL;
	String token; // For the online service: whose saves these are ("" for the signed-in player).
	HashSet<String> uploading;
	bool cloud = false; // The platform's cloud is a cloud store (Purr::cloud_store), not Steam Cloud.

	PurrSaveStore store; // This device's copy.

	static String file_name(const String &p_slot, const String &p_extension);
	Dictionary read_meta(const String &p_slot) const;
	void write_meta(const String &p_slot, const Dictionary &p_meta) const;
	PackedByteArray read_local(const String &p_slot) const;
	void write_local(const String &p_slot, const PackedByteArray &p_data) const;

	void upload(const String &p_slot);
	void uploaded(const Dictionary &p_result, const String &p_slot, int64_t p_edits_sent);
	void listed(const Dictionary &p_result);
	void downloaded(const Dictionary &p_result, const String &p_slot, int64_t p_version, int64_t p_edits);
	bool saved_meanwhile(const String &p_slot, int64_t p_edits, int64_t p_remote, const Dictionary &p_remote_summary);
	void steam_write(const String &p_slot);
	void take_slot(const Ref<PlayerSaves> &p_from, const String &p_slot);
	void merge_remote_achievements(const PackedByteArray &p_remote, const String &p_key, int64_t p_remote_version);

	void cloud_upload(const String &p_slot);
	void cloud_uploaded(const Dictionary &p_result, const String &p_slot, int64_t p_edits_sent);
	void cloud_listed(const Dictionary &p_result);
	void cloud_download(const String &p_slot);
	void cloud_downloaded(const Dictionary &p_result, const String &p_slot, int64_t p_edits);

protected:
	static void _bind_methods();

public:
	// Slot names the online services and every platform's cloud can store: 1 to 64 of a-z, 0-9, - and _.
	static bool is_valid_slot(const String &p_slot);
	// Two achievement lists ({id: progress}) as one, with each achievement's furthest progress.
	static Dictionary merge_achievements(const Variant &p_a, const Variant &p_b);

	void setup(const String &p_owner, Backend p_backend, const String &p_token);
	Backend get_backend() const { return backend; }
	String get_owner() const { return owner; }
	PackedStringArray get_slots() const;
	// The game's own slots: every one but the achievements'.
	PackedStringArray get_game_slots() const;

	void save(const String &p_slot, const Variant &p_data, const Dictionary &p_summary);
	Variant load(const String &p_slot) const;
	bool has(const String &p_slot) const;
	Dictionary get_summary(const String &p_slot) const;

	// Brings this device and the backend in line: newer saves come down, unsynced ones go up.
	void sync();
	// After a conflict: keep this device's save (uploading it over the other) or the other one.
	void resolve_conflict(const String &p_slot, bool p_keep_local);

	// Takes every save of another player's saves (or of these saves under another backend) as
	// this device's own, not yet synced, then syncs: when a player adds an email, their saves go
	// up to the account, and the conflict signal fires for any slot the account already has.
	void adopt(const Ref<PlayerSaves> &p_from);
	// Moves the saves another owner kept for this same player (before their account was known on
	// this device) into these, without syncing: slots these saves already have stay as they are,
	// except the achievements, which are merged. What moves is gone from the other owner.
	void carry_over(const Ref<PlayerSaves> &p_from);
	// Deletes every save on this device, and in the platform's cloud (a forgotten local profile);
	// never the online service's.
	void delete_all();
	// Deletes one slot on this device and in the platform's cloud; never the online service's.
	void delete_slot(const String &p_slot);
	// Whether any save on this device hasn't reached the backend yet.
	bool has_unsynced() const;
};

VARIANT_ENUM_CAST(PlayerSaves::Backend);
