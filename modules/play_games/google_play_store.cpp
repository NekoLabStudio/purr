#include "google_play_store.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"

Object *GooglePlayStoreBackend::android() const {
	return Engine::get_singleton()->has_singleton("PurrStoreAndroid") ? Engine::get_singleton()->get_singleton_object("PurrStoreAndroid") : nullptr;
}

// Godot registers Java plugins after the modules, so the plugin is looked up when first used.
bool GooglePlayStoreBackend::connect_android() {
	Object *plugin = android();
	if (!plugin) {
		return false;
	}
	if (!plugin->is_connected(SNAME("finished"), callable_mp(this, &GooglePlayStoreBackend::finished))) {
		plugin->connect(SNAME("finished"), callable_mp(this, &GooglePlayStoreBackend::finished));
		plugin->connect(SNAME("changed"), callable_mp(this, &GooglePlayStoreBackend::plugin_changed));
	}
	return true;
}

bool GooglePlayStoreBackend::is_available() const {
	Object *plugin = android();
	return plugin && (bool)plugin->call(SNAME("isAvailable"));
}

void GooglePlayStoreBackend::start(const PackedStringArray &p_product_ids, const Callable &p_changed) {
	changed = p_changed;
	if (connect_android()) {
		android()->call(SNAME("start"), p_product_ids);
	}
}

void GooglePlayStoreBackend::plugin_changed(const Dictionary &p_owned, const Dictionary &p_prices) {
	owned.clear();
	for (const KeyValue<Variant, Variant> &entry : p_owned) {
		owned.insert(entry.key);
	}
	for (const KeyValue<Variant, Variant> &entry : p_prices) {
		prices[entry.key] = entry.value;
	}
	if (changed.is_valid()) {
		changed.call();
	}
}

String GooglePlayStoreBackend::get_price(const String &p_product_id) const {
	const HashMap<String, String>::ConstIterator price = prices.find(p_product_id);
	return price ? price->value : String();
}

int GooglePlayStoreBackend::add_call(const Callable &p_done) {
	const int id = next_call_id++;
	pending[id] = p_done;
	return id;
}

void GooglePlayStoreBackend::finished(int p_call_id, const Dictionary &p_result) {
	const HashMap<int, Callable>::Iterator call = pending.find(p_call_id);
	if (!call) {
		return;
	}
	const Callable done = call->value;
	pending.remove(call);
	done.call(p_result);
}

void GooglePlayStoreBackend::purchase(const String &p_product_id, const Callable &p_done) {
	if (!connect_android()) {
		Dictionary result;
		result["ok"] = false;
		result["error"] = "unavailable";
		result["message"] = "Google Play's store isn't in this build.";
		p_done.call_deferred(result);
		return;
	}
	android()->call(SNAME("purchase"), add_call(p_done), p_product_id);
}

void GooglePlayStoreBackend::restore(const Callable &p_done) {
	if (!connect_android()) {
		Dictionary result;
		result["ok"] = false;
		result["error"] = "unavailable";
		result["message"] = "Google Play's store isn't in this build.";
		p_done.call_deferred(result);
		return;
	}
	android()->call(SNAME("restore"), add_call(p_done));
}

// A Play Integrity token whose requestHash is the service's challenge; both go to the service.
void GooglePlayStoreBackend::get_ownership_proof(const String &p_challenge, const Callable &p_done) {
	const String project = String(GLOBAL_GET("google_play/cloud_project_number")).strip_edges();
	if (!connect_android() || project.is_empty()) {
		p_done.call_deferred(Dictionary());
		return;
	}
	android()->call(SNAME("integrityToken"), add_call(callable_mp(this, &GooglePlayStoreBackend::token_received).bind(p_done, p_challenge)), project, p_challenge);
}

void GooglePlayStoreBackend::token_received(const Dictionary &p_result, const Callable &p_done, const String &p_challenge) {
	Dictionary proof;
	if (p_result.get("ok", false)) {
		proof["play_integrity"] = p_result.get("token", "");
		proof["challenge"] = p_challenge;
	}
	p_done.call(proof);
}
