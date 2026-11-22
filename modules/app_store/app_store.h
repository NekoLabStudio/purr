#pragma once

// The App Store for Store, on Apple platforms (StoreKit 2, in store_kit.swift): what the player
// owns, prices, purchases, restoring, and the AppTransaction for the online service.

#include "modules/purr/purr_store_backend.h"

#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

class AppStoreBackend : public PurrStoreBackend {
	GDCLASS(AppStoreBackend, PurrStoreBackend);

	HashSet<String> owned;
	HashMap<String, String> prices;
	Callable changed;
	uint64_t proof_request = 0; // The ownership proof being waited for.

	void loaded(const PackedStringArray &p_owned, const Dictionary &p_prices);
	void proof_received(const String &p_jws, uint64_t p_request, const Callable &p_done);

protected:
	static void _bind_methods() {}

public:
	String get_platform() const override { return "apple"; }
	bool is_available() const override;
	void start(const PackedStringArray &p_product_ids, const Callable &p_changed) override;
	bool owns(const String &p_product_id) const override { return owned.has(p_product_id); }
	String get_price(const String &p_product_id) const override;
	void purchase(const String &p_product_id, const Callable &p_done) override;
	void restore(const Callable &p_done) override;
	void get_ownership_proof(const String &p_challenge, const Callable &p_done) override;
};
