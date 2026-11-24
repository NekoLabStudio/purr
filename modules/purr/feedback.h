#pragma once

// Feedback: a player's few words to the game's developer, in a category the game names (a bug, an
// idea, a request for help), sent to the online services with what the game runs on. Anonymous
// unless feedback/<category>/named is on in Project Settings: then it names the player's account,
// so the developer can follow up. The operator reads it with npm run feedback.

#include "core/object/class_db.h"
#include "core/object/object.h"

class Feedback : public Object {
	GDCLASS(Feedback, Object);

	static Feedback *singleton;

protected:
	static void _bind_methods();

public:
	// The service's limit, in characters (MAX_FEEDBACK_TEXT in online/src/feedback.ts).
	static constexpr int MAX_TEXT = 2000;

	static Feedback *get_singleton() { return singleton; }

	bool is_available() const;
	Signal send(const String &p_category, const String &p_text, bool p_include_log = false);

	Feedback();
	~Feedback();
};
