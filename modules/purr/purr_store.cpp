#include "purr_store.h"

#include "purr.h"
#include "store.h"

#include "modules/purr_online/purr_online.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"

Ref<PurrStoreBackend> PurrStore::backend;

namespace {

const char *SETTINGS_PREFIX = "store/";

Dictionary result_of(const String &p_error = String(), const String &p_message = String()) {
	Dictionary result;
	result["ok"] = p_error.is_empty();
	if (!p_error.is_empty()) {
		result["error"] = p_error;
		result["message"] = p_message;
	}
	return result;
}

void call_later(const Callable &p_done, const Variant &p_result) {
	p_done.call_deferred(p_result);
}

} // namespace

PurrStore::~PurrStore() {
	Purr *purr = Purr::get_singleton();
	Object *steam = purr ? purr->steam() : nullptr;
	if (steam && steam->is_connected(SNAME("dlc_installed"), callable_mp(this, &PurrStore::steam_dlc_installed))) {
		steam->disconnect(SNAME("dlc_installed"), callable_mp(this, &PurrStore::steam_dlc_installed));
		steam->disconnect(SNAME("overlay_toggled"), callable_mp(this, &PurrStore::steam_overlay_toggled));
	}
}

// The game's IDs: every store/<id>/... in Project Settings.
PackedStringArray PurrStore::ids() const {
	List<PropertyInfo> properties;
	ProjectSettings::get_singleton()->get_property_list(&properties);
	PackedStringArray found;
	for (const PropertyInfo &property : properties) {
		if (!property.name.begins_with(SETTINGS_PREFIX)) {
			continue;
		}
		const String rest = property.name.substr(strlen(SETTINGS_PREFIX));
		if (rest.contains("/")) {
			const String id = rest.get_slicec('/', 0);
			if (!found.has(id)) {
				found.push_back(id);
			}
		}
	}
	return found;
}

String PurrStore::product(const String &p_id, const String &p_platform) const {
	return String(ProjectSettings::get_singleton()->get_setting(SETTINGS_PREFIX + p_id + "/" + p_platform, String())).strip_edges();
}

int64_t PurrStore::steam_app_id(const String &p_id) const {
	const String app_id = product(p_id, "steam");
	return app_id.is_valid_int() ? app_id.to_int() : 0;
}

// Picked on first use, once Steam can have started: Steam where it runs, then the platform's
// store, then the editor's pretend one.
void PurrStore::start() {
	if (started) {
		return;
	}
	started = true;
	Purr *purr = Purr::get_singleton();
	if (purr->start_steam()) {
		seller = SELLER_STEAM;
		Object *steam = purr->steam();
		steam->connect(SNAME("dlc_installed"), callable_mp(this, &PurrStore::steam_dlc_installed));
		steam->connect(SNAME("overlay_toggled"), callable_mp(this, &PurrStore::steam_overlay_toggled));
	} else if (backend.is_valid() && backend->is_available()) {
		seller = SELLER_BACKEND;
		PackedStringArray products;
		for (const String &id : ids()) {
			const String name = product(id, backend->get_platform());
			if (!name.is_empty()) {
				products.push_back(name);
			}
		}
		backend->start(products, callable_mp(this, &PurrStore::backend_changed));
	} else if (OS::get_singleton()->has_feature("editor")) {
		seller = SELLER_EDITOR;
	}
	for (const String &id : ids()) {
		if (owns_now(id)) {
			owned.insert(id);
		}
	}
}

bool PurrStore::owns_now(const String &p_id) const {
	switch (seller) {
		case SELLER_STEAM: {
			const int64_t app_id = steam_app_id(p_id);
			return app_id > 0 && (bool)Purr::get_singleton()->steam()->call("isDLCInstalled", app_id);
		}
		case SELLER_BACKEND: {
			const String name = product(p_id, backend->get_platform());
			return !name.is_empty() && backend->owns(name);
		}
		case SELLER_EDITOR: {
			const PackedStringArray listed = GLOBAL_GET("store/owned_in_editor");
			return listed.has(p_id) || bought_in_editor.has(p_id);
		}
		default:
			return false;
	}
}

// Looks again, and tells the game about whatever changed (bought, or refunded).
void PurrStore::refresh() {
	for (const String &id : ids()) {
		const bool now = owns_now(id);
		if (now != owned.has(id)) {
			if (now) {
				owned.insert(id);
			} else {
				owned.erase(id);
			}
			Store::get_singleton()->call_deferred(SNAME("emit_signal"), SNAME("ownership_changed"), id);
		}
	}
}

void PurrStore::backend_changed() {
	refresh();
}

bool PurrStore::is_available() {
	start();
	return seller != SELLER_NONE;
}

bool PurrStore::owns(const String &p_id) {
	start();
	return owned.has(p_id);
}

String PurrStore::get_price(const String &p_id) {
	start();
	if (seller == SELLER_BACKEND) {
		const String name = product(p_id, backend->get_platform());
		return name.is_empty() ? String() : backend->get_price(name);
	}
	// Steam doesn't tell games a DLC's price: its store page in the overlay shows it.
	return String();
}

void PurrStore::purchase(const String &p_id, const Callable &p_done) {
	start();
	if (!ids().has(p_id)) {
		call_later(p_done, result_of("unavailable", vformat("\"%s\" isn't in the Store tab of Project Settings.", p_id)));
		return;
	}
	if (owned.has(p_id)) {
		call_later(p_done, result_of());
		return;
	}
	switch (seller) {
		case SELLER_STEAM: {
			const int64_t app_id = steam_app_id(p_id);
			if (app_id <= 0) {
				call_later(p_done, result_of("unavailable", vformat("\"%s\" has no Steam app ID (store/%s/steam).", p_id, p_id)));
				return;
			}
			if (!steam_pending_id.is_empty()) {
				call_later(p_done, result_of("failed", "Another purchase is already under way."));
				return;
			}
			Object *steam = Purr::get_singleton()->steam();
			if (!(bool)steam->call("isOverlayEnabled")) {
				// No overlay (the player turned it off): the store page opens in Steam instead, and
				// ownership_changed says if it's bought there.
				OS::get_singleton()->shell_open(vformat("steam://store/%d", app_id));
				call_later(p_done, result_of("pending", "The DLC's page opened in Steam; Store.ownership_changed fires if the player buys it."));
				return;
			}
			steam_pending_id = p_id;
			steam_pending_done = p_done;
			steam->call("activateGameOverlayToStore", app_id, 0);
		} break;
		case SELLER_BACKEND: {
			const String name = product(p_id, backend->get_platform());
			if (name.is_empty()) {
				call_later(p_done, result_of("unavailable", vformat("\"%s\" has no product on this store (store/%s/%s).", p_id, p_id, backend->get_platform())));
				return;
			}
			backend->purchase(name, p_done);
		} break;
		case SELLER_EDITOR: {
			print_line(vformat("Store: pretending \"%s\" was bought (in the editor, purchases always succeed).", p_id));
			bought_in_editor.insert(p_id);
			refresh();
			call_later(p_done, result_of());
		} break;
		default:
			call_later(p_done, result_of("unavailable", "There's no store here: Steam isn't running and this platform has none."));
	}
}

void PurrStore::restore(const Callable &p_done) {
	start();
	if (seller == SELLER_BACKEND) {
		backend->restore(p_done);
		return;
	}
	if (seller == SELLER_NONE) {
		call_later(p_done, result_of("unavailable", "There's no store here: Steam isn't running and this platform has none."));
		return;
	}
	// Steam always knows what the player owns.
	refresh();
	call_later(p_done, result_of());
}

// Steam: the overlay closed (the player bought it or not), or a DLC arrived.

void PurrStore::steam_dlc_installed(int64_t p_app_id) {
	refresh();
	if (!steam_pending_id.is_empty() && steam_app_id(steam_pending_id) == p_app_id) {
		finish_steam_purchase();
	}
}

void PurrStore::steam_overlay_toggled(bool p_active, bool p_user_initiated, int64_t p_app_id) {
	if (!p_active && !steam_pending_id.is_empty()) {
		refresh();
		finish_steam_purchase();
	}
}

void PurrStore::finish_steam_purchase() {
	const Callable done = steam_pending_done;
	const bool bought = owned.has(steam_pending_id);
	steam_pending_id = String();
	steam_pending_done = Callable();
	call_later(done, bought ? result_of() : result_of("cancelled", "The player closed the store page without buying it."));
}

// Proof for the online service.

void PurrStore::get_ownership_proof(const Callable &p_done) {
	start();
	if (seller == SELLER_STEAM) {
		Object *steam = Purr::get_singleton()->steam();
		steam->connect(SNAME("get_ticket_for_web_api"), callable_mp(this, &PurrStore::steam_ticket).bind(p_done), CONNECT_ONE_SHOT);
		steam->call("getAuthTicketForWebApi", "trill");
	} else if (seller == SELLER_BACKEND && backend->get_platform() == "google_play") {
		// Play's proof is bound to one request: the service's challenge.
		Signal challenge = PurrOnline::get_singleton()->get_ownership_challenge();
		challenge.connect(callable_mp(this, &PurrStore::challenge_received).bind(p_done), CONNECT_ONE_SHOT);
	} else if (seller == SELLER_BACKEND) {
		backend->get_ownership_proof(String(), p_done);
	} else {
		call_later(p_done, Dictionary());
	}
}

void PurrStore::steam_ticket(int64_t p_auth_ticket, int64_t p_result, int64_t p_size, const PackedByteArray &p_ticket, const Callable &p_done) {
	Dictionary proof;
	if (p_result == Purr::get_singleton()->steam_constant("RESULT_OK")) {
		proof["steam_ticket"] = String::hex_encode_buffer(p_ticket.ptr(), p_size);
	}
	call_later(p_done, proof);
}

void PurrStore::challenge_received(const Dictionary &p_result, const Callable &p_done) {
	const String challenge = p_result.get("ok", false) ? String(Dictionary(p_result.get("data", Dictionary())).get("challenge", "")) : String();
	if (challenge.is_empty()) {
		call_later(p_done, Dictionary());
		return;
	}
	backend->get_ownership_proof(challenge, p_done);
}
