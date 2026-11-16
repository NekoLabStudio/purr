#include "account.h"

#include "purr.h"

#include "core/object/class_db.h"

Account *Account::singleton = nullptr;

Account::Provider Account::provider_from(const String &p_name) {
	if (p_name == "steam") {
		return PROVIDER_STEAM;
	}
	if (p_name == "game_center") {
		return PROVIDER_GAME_CENTER;
	}
	if (p_name == "play_games") {
		return PROVIDER_PLAY_GAMES;
	}
	if (p_name == "device") {
		return PROVIDER_DEVICE;
	}
	return PROVIDER_LOCAL;
}

Signal Account::sign_in() {
	return Purr::get_singleton()->sign_in();
}

bool Account::is_signed_in() const {
	return Purr::get_singleton()->is_signed_in();
}

Account::Provider Account::get_provider() const {
	return provider_from(Purr::get_singleton()->get_provider());
}

String Account::get_player_name() const {
	return Purr::get_singleton()->get_player_name();
}

bool Account::needs_display_name() const {
	return Purr::get_singleton()->needs_display_name();
}

Signal Account::set_display_name(const String &p_name) {
	return Purr::get_singleton()->set_display_name(p_name);
}

bool Account::can_link_email() const {
	return Purr::get_singleton()->is_available("linking");
}

bool Account::has_email() const {
	return Purr::get_singleton()->has_email();
}

Signal Account::link_email(const String &p_email, const String &p_current_email) {
	return Purr::get_singleton()->email_link_start(p_email, p_current_email);
}

Signal Account::confirm_email(const String &p_code) {
	return Purr::get_singleton()->email_link_finish(p_code);
}

Signal Account::get_devices() {
	return Purr::get_singleton()->get_devices();
}

Signal Account::forget_device(const String &p_device_id) {
	return Purr::get_singleton()->forget_device(p_device_id);
}

String Account::get_device_label() const {
	return Purr::get_singleton()->get_device_label();
}

void Account::set_device_label(const String &p_label) {
	Purr::get_singleton()->set_device_label(p_label);
}

Signal Account::delete_account() {
	return Purr::get_singleton()->delete_account();
}

void Account::_bind_methods() {
	ClassDB::bind_method(D_METHOD("sign_in"), &Account::sign_in);
	ClassDB::bind_method(D_METHOD("is_signed_in"), &Account::is_signed_in);
	ClassDB::bind_method(D_METHOD("get_provider"), &Account::get_provider);
	ClassDB::bind_method(D_METHOD("get_player_name"), &Account::get_player_name);
	ClassDB::bind_method(D_METHOD("needs_display_name"), &Account::needs_display_name);
	ClassDB::bind_method(D_METHOD("set_display_name", "name"), &Account::set_display_name);
	ClassDB::bind_method(D_METHOD("can_link_email"), &Account::can_link_email);
	ClassDB::bind_method(D_METHOD("has_email"), &Account::has_email);
	ClassDB::bind_method(D_METHOD("link_email", "email", "current_email"), &Account::link_email, DEFVAL(String()));
	ClassDB::bind_method(D_METHOD("confirm_email", "code"), &Account::confirm_email);
	ClassDB::bind_method(D_METHOD("get_devices"), &Account::get_devices);
	ClassDB::bind_method(D_METHOD("forget_device", "device_id"), &Account::forget_device);
	ClassDB::bind_method(D_METHOD("get_device_label"), &Account::get_device_label);
	ClassDB::bind_method(D_METHOD("set_device_label", "label"), &Account::set_device_label);
	ClassDB::bind_method(D_METHOD("delete_account"), &Account::delete_account);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "provider", PROPERTY_HINT_ENUM, "Local,Device,Steam,Game Center,Play Games", PROPERTY_USAGE_NONE), "", "get_provider");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "player_name", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE), "", "get_player_name");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "device_label"), "set_device_label", "get_device_label");

	ADD_SIGNAL(MethodInfo("signed_in"));
	ADD_SIGNAL(MethodInfo("update_required", PropertyInfo(Variant::NIL, "min_version", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT), PropertyInfo(Variant::NIL, "max_version", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
	ADD_SIGNAL(MethodInfo("service_outdated"));

	BIND_ENUM_CONSTANT(PROVIDER_LOCAL);
	BIND_ENUM_CONSTANT(PROVIDER_DEVICE);
	BIND_ENUM_CONSTANT(PROVIDER_STEAM);
	BIND_ENUM_CONSTANT(PROVIDER_GAME_CENTER);
	BIND_ENUM_CONSTANT(PROVIDER_PLAY_GAMES);
}

Account::Account() {
	singleton = this;
}

Account::~Account() {
	singleton = nullptr;
}
