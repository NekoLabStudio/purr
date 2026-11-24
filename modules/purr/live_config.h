#pragma once

// LiveConfig: values the game reads that the developer changes on their online services instance
// (npm run live-config) without shipping an update: feature flags, tuning, seasonal events. Each
// key's default is live_config/<key> in Project Settings. Values come from the service the first
// time the game asks for one, then every 15 minutes and when the game comes back to the front;
// scheduled ones switch on time, by the service's clock. The last values are kept on the device,
// for starting offline.

#include "core/object/class_db.h"
#include "core/object/object.h"
#include "core/object/ref_counted.h"

class LiveConfig : public Object {
	GDCLASS(LiveConfig, Object);

	static LiveConfig *singleton;

	Array values; // The service's, in effect or scheduled: [{"key", "value", "starts_at", "ends_at"}].
	int64_t clock_offset = 0; // The service's clock less this device's, in seconds.
	Dictionary in_effect; // Of those, each key's value now.
	bool loaded = false;
	bool started = false;
	Ref<RefCounted> refreshing; // The refresh under way: a second refresh() meanwhile shares it.
	uint64_t schedule_generation = 0;
	uint64_t last_refresh_msec = 0;

	void start();
	void refreshed(const Dictionary &p_result, const Ref<RefCounted> &p_call);
	void use(const Array &p_values, int64_t p_clock_offset);
	void update_in_effect();
	void schedule_switch();
	void switch_due(uint64_t p_generation);
	void refresh_every();
	int64_t service_time() const;

protected:
	static void _bind_methods();

public:
	static LiveConfig *get_singleton() { return singleton; }

	// The key's value now: the service's, else live_config/<key> in Project Settings, else p_default.
	Variant get_value(const String &p_key, const Variant &p_default = Variant());
	// Whether values have come from the service, this run or an earlier one.
	bool is_loaded();
	Signal refresh();
	// From PurrAppWatcher: the game came back to the front, so values may be old.
	void resumed();

	LiveConfig();
	~LiveConfig();
};
