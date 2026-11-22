#pragma once

// The store, for Purr (Store is what games see). A game's content is listed by its own IDs in
// Project Settings, each with its product on every store: store/<id>/steam (the DLC's app ID),
// store/<id>/apple and store/<id>/google_play (product IDs). It sells through Steam where Steam is
// running, otherwise through the platform's store (a PurrStoreBackend its module sets), otherwise,
// in the editor, pretends: store/owned_in_editor lists what's owned, and purchases succeed.
//
// It also proves to the online service that this copy came from a store, for games that only
// let players who bought them play online.

#include "purr_store_backend.h"

#include "core/object/ref_counted.h"
#include "core/templates/hash_set.h"

class PurrStore : public RefCounted {
	GDCLASS(PurrStore, RefCounted);

	static Ref<PurrStoreBackend> backend;

	enum Seller {
		SELLER_NONE,
		SELLER_STEAM,
		SELLER_BACKEND,
		SELLER_EDITOR,
	};

	Seller seller = SELLER_NONE;
	bool started = false;
	HashSet<String> owned; // What the player owned when last looked at, by the game's IDs.
	HashSet<String> bought_in_editor; // Pretend purchases, for this run.

	// A Steam purchase under way: the overlay's store page is open on it.
	String steam_pending_id;
	Callable steam_pending_done;

	void start();
	PackedStringArray ids() const;
	String product(const String &p_id, const String &p_platform) const;
	int64_t steam_app_id(const String &p_id) const;
	bool owns_now(const String &p_id) const;
	void refresh();
	void backend_changed();
	void steam_dlc_installed(int64_t p_app_id);
	void steam_overlay_toggled(bool p_active, bool p_user_initiated, int64_t p_app_id);
	void finish_steam_purchase();
	void steam_ticket(int64_t p_auth_ticket, int64_t p_result, int64_t p_size, const PackedByteArray &p_ticket, const Callable &p_done);
	void challenge_received(const Dictionary &p_result, const Callable &p_done);

protected:
	static void _bind_methods() {}

public:
	// The App Store's or Google Play's, set by its module at startup.
	static void set_backend(const Ref<PurrStoreBackend> &p_backend) { backend = p_backend; }

	bool is_available();
	bool owns(const String &p_id);
	String get_price(const String &p_id);
	// Both call p_done with {"ok"} or {"ok": false, "error", "message"}.
	void purchase(const String &p_id, const Callable &p_done);
	void restore(const Callable &p_done);
	// Calls p_done with this copy's proof of purchase for the online service ({"steam_ticket"},
	// {"app_transaction"} or {"play_integrity"}), or an empty Dictionary.
	void get_ownership_proof(const Callable &p_done);

	~PurrStore();
};
