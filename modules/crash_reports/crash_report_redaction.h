#pragma once

// What's taken out of a crash report before it leaves the device: the player's home folder (whose
// name is often theirs) and anything shaped like an email or IP address, which a log can pick up
// from the game's own messages. The online service removes addresses again, as it never keeps
// them.

#include "core/string/ustring.h"

String redact_crash_report(const String &p_report);
