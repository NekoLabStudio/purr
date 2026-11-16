#pragma once

// Achievements: player 1's achievements, on Steam, Game Center or Play Games, and kept with their
// saves so the platforms they play on later unlock them too. Each achievement's platform names are
// in Project Settings (achievements/<id>/<platform>).

#include "core/object/object.h"

class Achievements : public Object {
	GDCLASS(Achievements, Object);

	static Achievements *singleton;

protected:
	static void _bind_methods();

public:
	static Achievements *get_singleton() { return singleton; }

	bool is_available() const;
	void unlock(const String &p_id);
	void set_progress(const String &p_id, int64_t p_current, int64_t p_total);
	double get_progress(const String &p_id) const;
	Dictionary get_all() const;
	void show();

	Achievements();
	~Achievements();
};
