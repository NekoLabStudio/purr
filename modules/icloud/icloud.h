#pragma once

// ICloud: files in the player's iCloud through CloudKit's private database, on macOS, iOS and
// tvOS. Purr keeps saves here when there's no online account for them. Records live in one zone
// ("Purr") so they can be listed without any index set up in CloudKit's dashboard. Each
// asynchronous call returns a signal that fires once with a Dictionary: {"ok", "error", ...}.
//
// Off unless the game names its iCloud container in icloud/container and its export preset
// has the matching entitlements (see the class reference).

#include "core/object/object.h"
#include "core/object/ref_counted.h"
#include "core/templates/hash_set.h"

// One call in flight; its completed signal is what ICloud's methods return.
class ICloudCall : public RefCounted {
	GDCLASS(ICloudCall, RefCounted);

protected:
	static void _bind_methods();
};

class ICloud : public Object {
	GDCLASS(ICloud, Object);

	static ICloud *singleton;
	HashSet<Ref<ICloudCall>> pending;

	Signal fail_now(const String &p_error, const String &p_message);

protected:
	static void _bind_methods();

public:
	static ICloud *get_singleton() { return singleton; }

	// For the Objective-C side: a new call, and its result handed over on the main thread.
	Ref<ICloudCall> begin_call();
	void finish_call(const Ref<ICloudCall> &p_call, const Dictionary &p_result);

	String get_container() const;
	bool is_available() const;
	Signal save_file(const String &p_name, const PackedByteArray &p_data, const Dictionary &p_metadata = Dictionary());
	Signal load_file(const String &p_name);
	Signal list_files();
	Signal delete_file(const String &p_name);

	ICloud();
	~ICloud();
};
