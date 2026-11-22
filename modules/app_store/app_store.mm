#import <Foundation/Foundation.h>

#include "app_store.h"

#import "purr_app_store-Swift.gen.h"

#include "core/object/callable_mp.h"
#include "scene/main/scene_tree.h"

namespace {

// How long the online service's sign-in waits for the AppTransaction (it can need the network).
constexpr double PROOF_TIMEOUT = 10.0;

String ns_string(NSString *p_string) {
	return p_string ? String::utf8(p_string.UTF8String) : String();
}

Dictionary result_of(NSString *p_error, NSString *p_message) {
	Dictionary result;
	result["ok"] = p_error.length == 0;
	if (p_error.length > 0) {
		result["error"] = ns_string(p_error);
		result["message"] = ns_string(p_message);
	}
	return result;
}

} // namespace

bool AppStoreBackend::is_available() const {
	return [PurrStoreKit isAvailable];
}

void AppStoreBackend::start(const PackedStringArray &p_product_ids, const Callable &p_changed) {
	changed = p_changed;
	NSMutableArray<NSString *> *ids = [NSMutableArray array];
	for (const String &id : p_product_ids) {
		[ids addObject:[NSString stringWithUTF8String:id.utf8().get_data()]];
	}
	AppStoreBackend *self_backend = this;
	[PurrStoreKit start:ids
				changed:^(NSArray<NSString *> *p_owned, NSDictionary<NSString *, NSString *> *p_prices) {
					PackedStringArray now_owned;
					for (NSString *id in p_owned) {
						now_owned.push_back(ns_string(id));
					}
					Dictionary now_prices;
					for (NSString *id in p_prices) {
						now_prices[ns_string(id)] = ns_string(p_prices[id]);
					}
					callable_mp(self_backend, &AppStoreBackend::loaded).call_deferred(now_owned, now_prices);
				}];
}

void AppStoreBackend::loaded(const PackedStringArray &p_owned, const Dictionary &p_prices) {
	owned.clear();
	for (const String &id : p_owned) {
		owned.insert(id);
	}
	for (const KeyValue<Variant, Variant> &entry : p_prices) {
		prices[entry.key] = entry.value;
	}
	if (changed.is_valid()) {
		changed.call();
	}
}

String AppStoreBackend::get_price(const String &p_product_id) const {
	const HashMap<String, String>::ConstIterator price = prices.find(p_product_id);
	return price ? price->value : String();
}

void AppStoreBackend::purchase(const String &p_product_id, const Callable &p_done) {
	const Callable done = p_done;
	[PurrStoreKit purchase:[NSString stringWithUTF8String:p_product_id.utf8().get_data()]
					  done:^(NSString *p_error, NSString *p_message) {
						  done.call_deferred(result_of(p_error, p_message));
					  }];
}

void AppStoreBackend::restore(const Callable &p_done) {
	const Callable done = p_done;
	[PurrStoreKit restore:^(NSString *p_error, NSString *p_message) {
		done.call_deferred(result_of(p_error, p_message));
	}];
}

// The AppTransaction, or nothing if it doesn't come in time: signing in doesn't wait for ever.
void AppStoreBackend::get_ownership_proof(const String &p_challenge, const Callable &p_done) {
	const uint64_t request = ++proof_request;
	AppStoreBackend *self_backend = this;
	[PurrStoreKit appTransaction:^(NSString *p_jws) {
		callable_mp(self_backend, &AppStoreBackend::proof_received).call_deferred(ns_string(p_jws), request, p_done);
	}];
	if (SceneTree::get_singleton()) {
		SceneTree::get_singleton()->create_timer(PROOF_TIMEOUT, true, false, true)->connect(SNAME("timeout"), callable_mp(this, &AppStoreBackend::proof_received).bind(String(), request, p_done), CONNECT_ONE_SHOT);
	}
}

void AppStoreBackend::proof_received(const String &p_jws, uint64_t p_request, const Callable &p_done) {
	if (p_request != proof_request) {
		return; // Already answered (the timeout, or the AppTransaction).
	}
	proof_request++;
	Dictionary proof;
	if (!p_jws.is_empty()) {
		proof["app_transaction"] = p_jws;
	}
	p_done.call(proof);
}
