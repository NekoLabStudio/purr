#include "friends.h"

#include "purr.h"

#include "core/object/class_db.h"

Friends *Friends::singleton = nullptr;

bool Friends::is_available() const {
	return Purr::get_singleton()->is_available("friends");
}

Array Friends::get_all() const {
	return Purr::get_singleton()->get_friends();
}

void Friends::invite(const String &p_friend_id) {
	Purr::get_singleton()->invite_friend(p_friend_id);
}

void Friends::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_available"), &Friends::is_available);
	ClassDB::bind_method(D_METHOD("get_all"), &Friends::get_all);
	ClassDB::bind_method(D_METHOD("invite", "friend_id"), &Friends::invite);

	ADD_SIGNAL(MethodInfo("changed"));
}

Friends::Friends() {
	singleton = this;
}

Friends::~Friends() {
	singleton = nullptr;
}
