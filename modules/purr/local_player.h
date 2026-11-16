#pragma once

// A local player: player 1 (the device's signed-in account) or a guest. A plain guest has no saves
// of their own (their progress is part of player 1's save); a local profile is a guest remembered
// on this device, with saves kept like player 1's; an online guest brought their profile with
// their email, and reaches their account with a token that can only load and save.

#include "player_saves.h"

#include "core/object/class_db.h"
#include "core/object/ref_counted.h"

class LocalPlayer : public RefCounted {
	GDCLASS(LocalPlayer, RefCounted);

public:
	enum Kind {
		KIND_PLAYER, // Player 1.
		KIND_GUEST, // A plain guest.
		KIND_LOCAL_PROFILE, // A guest remembered on this device, without an account.
		KIND_ONLINE_GUEST, // A guest with an account, brought (and maybe remembered) with their email.
	};

private:
	Kind kind = KIND_GUEST;
	String player_name;
	String account_id;
	String profile_id; // A local profile's own ID on this device.
	String token;
	bool remembered = false;
	bool name_set = false; // A plain guest the game has named.
	Ref<PlayerSaves> saves;
	String pending_email;
	String pending_challenge;

	void email_code_sent(const Dictionary &p_result, const Ref<RefCounted> &p_call);
	void email_verified(const Dictionary &p_result, const Ref<RefCounted> &p_call);

protected:
	static void _bind_methods();

public:
	void setup(Kind p_kind, const String &p_name, const String &p_account_id, const String &p_token, bool p_remembered, PlayerSaves::Backend p_backend);
	void setup_local_profile(const String &p_name, const String &p_profile_id, PlayerSaves::Backend p_backend);

	Kind get_kind() const { return kind; }
	String get_player_name() const { return player_name; }
	void set_player_name(const String &p_name) { player_name = p_name; }
	String get_account_id() const { return account_id; }
	String get_profile_id() const { return profile_id; }
	String get_token() const { return token; }
	bool is_remembered() const { return remembered; }
	Ref<PlayerSaves> get_saves() const { return saves; }

	void unlock_achievement(const String &p_id);
	void set_achievement_progress(const String &p_id, int64_t p_current, int64_t p_total);
	Dictionary get_achievements() const;

	bool needs_display_name() const;
	Signal set_display_name(const String &p_name);

	Signal remember(const String &p_name);
	Signal email_link_start(const String &p_email);
	Signal email_link_finish(const String &p_code);
	void forget();
};

VARIANT_ENUM_CAST(LocalPlayer::Kind);
