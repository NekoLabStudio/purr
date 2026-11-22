#include "store.h"

#include "purr.h"

#include "core/object/class_db.h"

Store *Store::singleton = nullptr;

bool Store::is_available() const {
	return Purr::get_singleton()->store()->is_available();
}

bool Store::owns(const String &p_id) const {
	return Purr::get_singleton()->store()->owns(p_id);
}

String Store::get_price(const String &p_id) const {
	return Purr::get_singleton()->store()->get_price(p_id);
}

Signal Store::purchase(const String &p_id) {
	return Purr::get_singleton()->store_purchase(p_id);
}

Signal Store::restore() {
	return Purr::get_singleton()->store_restore();
}

void Store::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_available"), &Store::is_available);
	ClassDB::bind_method(D_METHOD("owns", "id"), &Store::owns);
	ClassDB::bind_method(D_METHOD("get_price", "id"), &Store::get_price);
	ClassDB::bind_method(D_METHOD("purchase", "id"), &Store::purchase);
	ClassDB::bind_method(D_METHOD("restore"), &Store::restore);

	ADD_SIGNAL(MethodInfo("ownership_changed", PropertyInfo(Variant::STRING, "id")));
}

Store::Store() {
	singleton = this;
}

Store::~Store() {
	singleton = nullptr;
}
