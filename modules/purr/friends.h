#pragma once

// Friends: the player's Steam friends, what they're playing and the lobbies they're in.

#include "core/object/object.h"

class Friends : public Object {
	GDCLASS(Friends, Object);

	static Friends *singleton;

protected:
	static void _bind_methods();

public:
	static Friends *get_singleton() { return singleton; }

	bool is_available() const;
	Array get_all() const;
	void invite(const String &p_friend_id);

	Friends();
	~Friends();
};
