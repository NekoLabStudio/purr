#pragma once

// Account: the player's account: signing in (on whatever platform the game runs), their name, the
// email that takes their progress to other platforms, and the devices that remember them.

#include "core/object/class_db.h"
#include "core/object/object.h"

class Account : public Object {
	GDCLASS(Account, Object);

	static Account *singleton;

protected:
	static void _bind_methods();

public:
	enum Provider {
		PROVIDER_LOCAL, // This device alone, without the online service.
		PROVIDER_DEVICE, // A device account on the online service.
		PROVIDER_STEAM,
		PROVIDER_GAME_CENTER,
		PROVIDER_PLAY_GAMES,
	};

	static Account *get_singleton() { return singleton; }
	static Provider provider_from(const String &p_name);

	Signal sign_in();
	bool is_signed_in() const;
	Provider get_provider() const;
	String get_player_name() const;
	bool needs_display_name() const;
	Signal set_display_name(const String &p_name);

	bool can_link_email() const;
	bool has_email() const;
	Signal link_email(const String &p_email, const String &p_current_email = String());
	Signal confirm_email(const String &p_code);

	Signal get_devices();
	Signal get_activity();
	Signal forget_device(const String &p_device_id);
	String get_device_label() const;
	void set_device_label(const String &p_label);

	Signal delete_account();

	Account();
	~Account();
};

VARIANT_ENUM_CAST(Account::Provider);
