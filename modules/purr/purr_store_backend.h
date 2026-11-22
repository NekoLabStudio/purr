#pragma once

// A store Store sells through where Steam isn't running: the App Store (the app_store module) or
// Google Play (the play_games module). The platform's module makes one at startup and hands it to
// PurrStore::set_backend(). Everything is called on the main thread, and every callback is called
// on the main thread too, later (never from inside the call).

#include "core/object/ref_counted.h"
#include "core/variant/callable.h"

class PurrStoreBackend : public RefCounted {
	GDCLASS(PurrStoreBackend, RefCounted);

protected:
	static void _bind_methods() {}

public:
	// "apple" or "google_play": the setting each product's ID is under (store/<id>/<platform>).
	virtual String get_platform() const = 0;
	// False where the store can't be reached at all (no Play Store on the device, say).
	virtual bool is_available() const = 0;
	// Loads what the player owns and the products' prices, then calls p_changed, and again
	// whenever either changes (a purchase on another device, a refund, Ask to Buy approved).
	virtual void start(const PackedStringArray &p_product_ids, const Callable &p_changed) = 0;
	virtual bool owns(const String &p_product_id) const = 0;
	// The price as the store shows it to this player ("£4.99"), or empty until it's known.
	virtual String get_price(const String &p_product_id) const = 0;
	// Calls p_done with {"ok"} or {"ok": false, "error": "cancelled" / "pending" / "unavailable" /
	// "failed", "message"}.
	virtual void purchase(const String &p_product_id, const Callable &p_done) = 0;
	virtual void restore(const Callable &p_done) = 0;
	// Calls p_done with what proves to the online service that this copy came from the store:
	// {"app_transaction": signed JWS} or {"play_integrity": token}, or an empty Dictionary when the
	// store can't say. p_challenge is the service's, for proofs bound to one request (Play).
	virtual void get_ownership_proof(const String &p_challenge, const Callable &p_done) = 0;
};
