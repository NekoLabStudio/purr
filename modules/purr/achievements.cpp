#include "achievements.h"

#include "purr.h"

#include "core/object/class_db.h"

Achievements *Achievements::singleton = nullptr;

bool Achievements::is_available() const {
	return Purr::get_singleton()->is_available("achievements");
}

void Achievements::unlock(const String &p_id) {
	Purr::get_singleton()->set_achievement_progress(p_id, 1, 1);
}

void Achievements::set_progress(const String &p_id, int64_t p_current, int64_t p_total) {
	Purr::get_singleton()->set_achievement_progress(p_id, p_current, p_total);
}

double Achievements::get_progress(const String &p_id) const {
	return Purr::get_singleton()->get_achievements().get(p_id, 0.0);
}

Dictionary Achievements::get_all() const {
	return Purr::get_singleton()->get_achievements();
}

void Achievements::show() {
	Purr::get_singleton()->show_achievements();
}

void Achievements::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_available"), &Achievements::is_available);
	ClassDB::bind_method(D_METHOD("unlock", "id"), &Achievements::unlock);
	ClassDB::bind_method(D_METHOD("set_progress", "id", "current", "total"), &Achievements::set_progress);
	ClassDB::bind_method(D_METHOD("get_progress", "id"), &Achievements::get_progress);
	ClassDB::bind_method(D_METHOD("get_all"), &Achievements::get_all);
	ClassDB::bind_method(D_METHOD("show"), &Achievements::show);

	ADD_SIGNAL(MethodInfo("changed"));
}

Achievements::Achievements() {
	singleton = this;
}

Achievements::~Achievements() {
	singleton = nullptr;
}
