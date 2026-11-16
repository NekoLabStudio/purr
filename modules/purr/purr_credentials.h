#pragma once

// Small secrets Purr keeps on the device (the device's own sign-in secret, sessions, remembered
// guests' device grants): in the Keychain on Apple platforms, the Credential Manager on Windows,
// and a file in user:// elsewhere and when running from the editor. Each game has its own, named
// after the project.

#include "core/string/ustring.h"

namespace PurrCredentials {

// p_household: on Apple TV, kept in the keychain all its users share (the game must export with
// the user-independent keychain, as Purr's tvOS exports do); elsewhere the same as the rest.
void store(const String &p_key, const String &p_value, bool p_household = false);
String load(const String &p_key, bool p_household = false);
void erase(const String &p_key, bool p_household = false);

// The name the game's secrets are kept under, from the project's name.
String service_name();

} // namespace PurrCredentials
