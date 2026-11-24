#pragma once

// Stats: counters each player adds to (deliveries, kilometres driven), kept on the device first
// and sent to the online services every few minutes and as the game goes to the background, like
// saves. The service adds every player's into a total, with a history by day. Each stat's limit
// on what one player adds in a day is stats/<id>/max_per_day in Project Settings, and its Steam
// stat, which gets the player's value too, stats/<id>/steam.

#include "core/object/class_db.h"
#include "core/object/object.h"

class Stats : public Object {
	GDCLASS(Stats, Object);

	static Stats *singleton;

	Dictionary values; // The player's: the service's last answer, with what's waiting added.
	Dictionary pending; // Added since, not yet sent.
	bool started = false;
	bool sending = false;
	bool fetched = false; // The service's values have come since the game started.
	bool steam_changed = false;

	void start();
	void tick();
	void fetch();
	void answered(const Dictionary &p_result, const Dictionary &p_sent);
	void save_file() const;
	void set_steam_stat(const String &p_id);
	static bool valid_id(const String &p_id);

protected:
	static void _bind_methods();

public:
	static Stats *get_singleton() { return singleton; }

	void add(const String &p_id, double p_amount);
	double get_value(const String &p_id);
	Signal get_total(const String &p_id);
	Signal get_history(const String &p_id, int p_days);
	// Sends what's waiting now (PurrAppWatcher, as the game goes to the background or closes).
	void send_now();

	Stats();
	~Stats();
};
