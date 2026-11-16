#pragma once

// LocalPlayers: everyone playing on this device: player 1 (the signed-in account) and any guests,
// with the guests and local profiles this device remembers.

#include "local_player.h"

#include "core/object/object.h"

class LocalPlayers : public Object {
	GDCLASS(LocalPlayers, Object);

	static LocalPlayers *singleton;

protected:
	static void _bind_methods();

public:
	static LocalPlayers *get_singleton() { return singleton; }

	Ref<LocalPlayer> get_player() const;
	Array get_players() const;
	Array get_remembered() const;
	Ref<LocalPlayer> add_guest(const Ref<LocalPlayer> &p_remembered = Ref<LocalPlayer>());
	void remove_guest(const Ref<LocalPlayer> &p_guest);

	bool can_invite_guests() const;
	Signal invite_guest(const String &p_email);
	Signal confirm_guest(const String &p_code, bool p_remember = false, const String &p_name = String());

	LocalPlayers();
	~LocalPlayers();
};
