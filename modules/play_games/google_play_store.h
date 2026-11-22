#pragma once

// Google Play's store for Store, on Android (the plugin's GooglePlayStorePlugin): what the player
// owns, prices, purchases (Play Billing), restoring, and a Play Integrity token for the online
// service.

#include "modules/purr/purr_store_backend.h"

#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

class GooglePlayStoreBackend : public PurrStoreBackend {
	GDCLASS(GooglePlayStoreBackend, PurrStoreBackend);

	HashSet<String> owned;
	HashMap<String, String> prices;
	Callable changed;
	int next_call_id = 1;
	HashMap<int, Callable> pending; // Calls to the plugin waiting for its answer.

	Object *android() const;
	bool connect_android();
	int add_call(const Callable &p_done);
	void finished(int p_call_id, const Dictionary &p_result);
	void plugin_changed(const Dictionary &p_owned, const Dictionary &p_prices);
	void token_received(const Dictionary &p_result, const Callable &p_done, const String &p_challenge);

protected:
	static void _bind_methods() {}

public:
	String get_platform() const override { return "google_play"; }
	bool is_available() const override;
	void start(const PackedStringArray &p_product_ids, const Callable &p_changed) override;
	bool owns(const String &p_product_id) const override { return owned.has(p_product_id); }
	String get_price(const String &p_product_id) const override;
	void purchase(const String &p_product_id, const Callable &p_done) override;
	void restore(const Callable &p_done) override;
	void get_ownership_proof(const String &p_challenge, const Callable &p_done) override;
};
