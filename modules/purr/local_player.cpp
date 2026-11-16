#include "local_player.h"

#include "purr.h"

#include "modules/purr_online/purr_online.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"


void LocalPlayer::setup(Kind p_kind, const String &p_name, const String &p_account_id, const String &p_token, bool p_remembered, PlayerSaves::Backend p_backend) {
	kind = p_kind;
	player_name = p_name;
	account_id = p_account_id;
	token = p_token;
	remembered = p_remembered;
	if (kind != KIND_GUEST) {
		saves.instantiate();
		saves->setup(account_id.is_empty() ? String("local") : account_id, p_backend, kind == KIND_PLAYER ? String() : token);
	}
}

void LocalPlayer::setup_local_profile(const String &p_name, const String &p_profile_id, PlayerSaves::Backend p_backend) {
	kind = KIND_LOCAL_PROFILE;
	player_name = p_name;
	profile_id = p_profile_id;
	account_id = String();
	token = String();
	remembered = true;
	saves.instantiate();
	saves->setup("profile_" + profile_id, p_backend, String());
}

// Player 1's achievements are Purr's (and go to the platform); anyone else's are kept on their
// account, for their own platform to unlock when they next play on their own device.
void LocalPlayer::unlock_achievement(const String &p_id) {
	set_achievement_progress(p_id, 1, 1);
}

void LocalPlayer::set_achievement_progress(const String &p_id, int64_t p_current, int64_t p_total) {
	if (kind == KIND_PLAYER) {
		Purr::get_singleton()->set_achievement_progress(p_id, p_current, p_total);
		return;
	}
	ERR_FAIL_COND_MSG(p_total <= 0, "An achievement's total progress is above 0.");
	PurrAchievements::record(saves, p_id, CLAMP((double)p_current / p_total, 0.0, 1.0));
}

Dictionary LocalPlayer::get_achievements() const {
	return PurrAchievements::read(saves);
}

// Names, by kind of player: player 1 is Purr's; a plain guest's is this device's to give (they
// start as "Guest"); a local profile's is kept with it on this device; a guest who brought their
// profile has their account's, which only they can change, from their own device.
bool LocalPlayer::needs_display_name() const {
	switch (kind) {
		case KIND_PLAYER:
			return Purr::get_singleton()->needs_display_name();
		case KIND_GUEST:
			return !name_set;
		case KIND_LOCAL_PROFILE:
			return false;
		case KIND_ONLINE_GUEST:
			return player_name.is_empty();
	}
	return false;
}

Signal LocalPlayer::set_display_name(const String &p_name) {
	if (kind == KIND_PLAYER) {
		return Purr::get_singleton()->set_display_name(p_name);
	}
	Dictionary result;
	const String name = p_name.strip_edges();
	if (kind == KIND_ONLINE_GUEST) {
		result["ok"] = false;
		result["error"] = "own_device";
		result["message"] = "A guest who brought their profile changes their name on their own device.";
	} else if (name.is_empty() || name.length() > Purr::MAX_NAME_LENGTH) {
		result["ok"] = false;
		result["error"] = "bad_name";
		result["message"] = vformat("A name is 1 to %d characters.", Purr::MAX_NAME_LENGTH);
	} else {
		player_name = name;
		name_set = true;
		if (kind == KIND_LOCAL_PROFILE) {
			Purr::get_singleton()->store_remembered(this);
		}
		result["ok"] = true;
		result["display_name"] = name;
	}
	return Purr::get_singleton()->deferred_result(result);
}

Signal LocalPlayer::remember(const String &p_name) {
	Dictionary result;
	result["ok"] = kind == KIND_GUEST || remembered;
	result["error"] = result["ok"] ? "" : "To remember a guest who brought their profile, bring it with remembering on.";
	if (kind == KIND_GUEST) {
		// A plain guest becomes a local profile: kept by this device, no account needed.
		uint8_t bytes[16];
		ERR_FAIL_COND_V(OS::get_singleton()->get_entropy(bytes, sizeof(bytes)) != OK, Purr::get_singleton()->deferred_result(Dictionary()));
		setup_local_profile(p_name, String::hex_encode_buffer(bytes, sizeof(bytes)), Purr::get_singleton()->local_profile_backend());
		Purr::get_singleton()->store_remembered(this);
	}
	return Purr::get_singleton()->deferred_result(result);
}

// A local profile adding an email: the code goes to the address, then email_link_finish() turns
// the profile into a remembered guest with an account, and uploads its saves to it.
Signal LocalPlayer::email_link_start(const String &p_email) {
	if (kind != KIND_LOCAL_PROFILE) {
		Dictionary result;
		result["ok"] = false;
		result["error"] = "not_local_profile";
		result["message"] = "Only a local profile adds an email this way; player 1 uses Account.link_email().";
		return Purr::get_singleton()->deferred_result(result);
	}
	pending_email = p_email.strip_edges();
	pending_challenge = String();
	Ref<PurrOnlineCall> call;
	call.instantiate();
	const Signal completed(call.ptr(), SNAME("completed"));
	// A code that can only bring the profile to an account, never sign in to it.
	Signal sent = PurrOnline::get_singleton()->guest_email_start(pending_email);
	sent.connect(callable_mp(this, &LocalPlayer::email_code_sent).bind(call), CONNECT_ONE_SHOT);
	return completed;
}

void LocalPlayer::email_code_sent(const Dictionary &p_result, const Ref<RefCounted> &p_call) {
	pending_challenge = p_result.get("ok", false) ? String(Dictionary(p_result["data"]).get("challenge", "")) : String();
	p_call->emit_signal(SNAME("completed"), p_result);
}

Signal LocalPlayer::email_link_finish(const String &p_code) {
	Ref<PurrOnlineCall> call;
	call.instantiate();
	const Signal completed(call.ptr(), SNAME("completed"));
	if (kind != KIND_LOCAL_PROFILE || pending_challenge.is_empty()) {
		Dictionary result;
		result["ok"] = false;
		result["error"] = "no_code_requested";
		result["message"] = "Call link_email() first.";
		purr_complete(call, result);
		return completed;
	}
	Signal verified = PurrOnline::get_singleton()->guest_email_verify(pending_email, pending_challenge, p_code, true, Purr::get_singleton()->get_device_label(), player_name);
	verified.connect(callable_mp(this, &LocalPlayer::email_verified).bind(call), CONNECT_ONE_SHOT);
	return completed;
}

void LocalPlayer::email_verified(const Dictionary &p_result, const Ref<RefCounted> &p_call) {
	if (p_result.get("ok", false)) {
		const Dictionary data = p_result["data"];
		const Ref<PlayerSaves> profile_saves = saves;
		setup(KIND_ONLINE_GUEST, player_name, data["account_id"], data["token"], true, PlayerSaves::BACKEND_ONLINE);
		profile_id = String();
		saves->adopt(profile_saves);
		profile_saves->delete_all();
		pending_email = String();
		pending_challenge = String();
		// Kept as a remembered guest from now on (the same entry, now with an account).
		Purr::get_singleton()->store_remembered(this);
	}
	p_call->emit_signal(SNAME("completed"), p_result);
}

void LocalPlayer::forget() {
	ERR_FAIL_COND_MSG(!remembered, "Only remembered guests and local profiles can be forgotten.");
	if (kind == KIND_LOCAL_PROFILE) {
		// A local profile lives only here: forgetting it deletes its progress.
		saves->delete_all();
	} else {
		PurrOnline::get_singleton()->forget_grant(token);
	}
	Purr::get_singleton()->forget_remembered(this);
	remembered = false;
}

void LocalPlayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_kind"), &LocalPlayer::get_kind);
	ClassDB::bind_method(D_METHOD("get_player_name"), &LocalPlayer::get_player_name);
	ClassDB::bind_method(D_METHOD("get_account_id"), &LocalPlayer::get_account_id);
	ClassDB::bind_method(D_METHOD("is_remembered"), &LocalPlayer::is_remembered);
	ClassDB::bind_method(D_METHOD("get_saves"), &LocalPlayer::get_saves);
	ClassDB::bind_method(D_METHOD("unlock_achievement", "id"), &LocalPlayer::unlock_achievement);
	ClassDB::bind_method(D_METHOD("set_achievement_progress", "id", "current", "total"), &LocalPlayer::set_achievement_progress);
	ClassDB::bind_method(D_METHOD("get_achievements"), &LocalPlayer::get_achievements);
	ClassDB::bind_method(D_METHOD("needs_display_name"), &LocalPlayer::needs_display_name);
	ClassDB::bind_method(D_METHOD("set_display_name", "name"), &LocalPlayer::set_display_name);
	ClassDB::bind_method(D_METHOD("remember", "name"), &LocalPlayer::remember);
	ClassDB::bind_method(D_METHOD("link_email", "email"), &LocalPlayer::email_link_start);
	ClassDB::bind_method(D_METHOD("confirm_email", "code"), &LocalPlayer::email_link_finish);
	ClassDB::bind_method(D_METHOD("forget"), &LocalPlayer::forget);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "kind", PROPERTY_HINT_ENUM, "Player,Guest,Local Profile,Online Guest", PROPERTY_USAGE_NONE), "", "get_kind");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "player_name", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_player_name");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "account_id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_account_id");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "remembered", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "is_remembered");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "saves", PROPERTY_HINT_RESOURCE_TYPE, "PlayerSaves", PROPERTY_USAGE_NONE), "", "get_saves");

	BIND_ENUM_CONSTANT(KIND_PLAYER);
	BIND_ENUM_CONSTANT(KIND_GUEST);
	BIND_ENUM_CONSTANT(KIND_LOCAL_PROFILE);
	BIND_ENUM_CONSTANT(KIND_ONLINE_GUEST);
}
