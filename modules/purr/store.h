#pragma once

// Store: content the player buys once and keeps (DLC, expansions, unlocks), the same on Steam, the
// App Store and Google Play. Each item's product on every store is in Project Settings
// (store/<id>/<platform>, the Store tab).

#include "core/object/object.h"

class Store : public Object {
	GDCLASS(Store, Object);

	static Store *singleton;

protected:
	static void _bind_methods();

public:
	static Store *get_singleton() { return singleton; }

	bool is_available() const;
	bool owns(const String &p_id) const;
	String get_price(const String &p_id) const;
	Signal purchase(const String &p_id);
	Signal restore();

	Store();
	~Store();
};
