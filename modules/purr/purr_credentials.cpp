#include "purr_credentials.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

// The file store: a ConfigFile in the game's user folder. Used where there's no system keychain,
// and when running from the editor: macOS ties Keychain access to the exact binary, so every new
// editor build would otherwise ask the developer's permission. Exported games, signed, use the
// system keychain.
namespace {

const char *FILE_PATH = "user://purr_credentials.cfg";

bool use_file() {
	return OS::get_singleton()->has_feature("editor");
}

void file_store(const String &p_key, const String &p_value) {
	Ref<ConfigFile> file;
	file.instantiate();
	file->load(FILE_PATH);
	file->set_value("credentials", p_key, p_value);
	file->save(FILE_PATH);
	// Readable by this user only, where the system has such permissions.
	FileAccess::set_unix_permissions(FILE_PATH, 0600);
}

String file_load(const String &p_key) {
	Ref<ConfigFile> file;
	file.instantiate();
	file->load(FILE_PATH);
	return file->get_value("credentials", p_key, String());
}

void file_erase(const String &p_key) {
	Ref<ConfigFile> file;
	file.instantiate();
	if (file->load(FILE_PATH) == OK && file->has_section_key("credentials", p_key)) {
		file->erase_section_key("credentials", p_key);
		file->save(FILE_PATH);
	}
}

} // namespace

String PurrCredentials::service_name() {
	return "Purr: " + String(GLOBAL_GET("application/config/name"));
}

#if defined(APPLE_EMBEDDED_ENABLED) || defined(MACOS_ENABLED)

#include <Security/Security.h>

namespace {

CFMutableDictionaryRef query(const String &p_key, bool p_household) {
	CFMutableDictionaryRef result = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	const CharString service = PurrCredentials::service_name().utf8();
	const CharString account = p_key.utf8();
	CFStringRef service_ref = CFStringCreateWithCString(nullptr, service.get_data(), kCFStringEncodingUTF8);
	CFStringRef account_ref = CFStringCreateWithCString(nullptr, account.get_data(), kCFStringEncodingUTF8);
	CFDictionarySetValue(result, kSecClass, kSecClassGenericPassword);
	CFDictionarySetValue(result, kSecAttrService, service_ref);
	CFDictionarySetValue(result, kSecAttrAccount, account_ref);
#ifdef TVOS_ENABLED
	if (p_household) {
		CFDictionarySetValue(result, kSecUseUserIndependentKeychain, kCFBooleanTrue);
	}
#endif
	CFRelease(service_ref);
	CFRelease(account_ref);
	return result;
}

} // namespace

void PurrCredentials::store(const String &p_key, const String &p_value, bool p_household) {
	if (use_file()) {
		file_store(p_key, p_value);
		return;
	}
	// Updated in place, added only if it isn't there: deleting first and then failing to add (the
	// Keychain locked, say) would lose what was kept, such as the device's own account.
	const CharString value = p_value.utf8();
	CFDataRef data = CFDataCreate(nullptr, (const UInt8 *)value.get_data(), value.length());
	CFMutableDictionaryRef search = query(p_key, p_household);
	CFMutableDictionaryRef changes = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFDictionarySetValue(changes, kSecValueData, data);
	OSStatus status = SecItemUpdate(search, changes);
	CFRelease(changes);
	CFRelease(search);
	if (status == errSecItemNotFound) {
		CFMutableDictionaryRef item = query(p_key, p_household);
		CFDictionarySetValue(item, kSecValueData, data);
		CFDictionarySetValue(item, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlock);
		status = SecItemAdd(item, nullptr);
		CFRelease(item);
	}
	CFRelease(data);
	ERR_FAIL_COND_MSG(status != errSecSuccess, vformat("Couldn't keep \"%s\" in the Keychain (OSStatus %d).", p_key, (int)status));
}

String PurrCredentials::load(const String &p_key, bool p_household) {
	if (use_file()) {
		return file_load(p_key);
	}
	CFMutableDictionaryRef search = query(p_key, p_household);
	CFDictionarySetValue(search, kSecReturnData, kCFBooleanTrue);
	CFDictionarySetValue(search, kSecMatchLimit, kSecMatchLimitOne);
	CFTypeRef found = nullptr;
	const OSStatus status = SecItemCopyMatching(search, &found);
	CFRelease(search);
	if (status != errSecSuccess || !found) {
		return String();
	}
	const String value = String::utf8((const char *)CFDataGetBytePtr((CFDataRef)found), CFDataGetLength((CFDataRef)found));
	CFRelease(found);
	return value;
}

void PurrCredentials::erase(const String &p_key, bool p_household) {
	if (use_file()) {
		file_erase(p_key);
		return;
	}
	CFMutableDictionaryRef search = query(p_key, p_household);
	SecItemDelete(search);
	CFRelease(search);
}

#elif defined(WINDOWS_ENABLED)

#include <windows.h>
#include <wincred.h>

namespace {

String target(const String &p_key) {
	return PurrCredentials::service_name() + "/" + p_key;
}

} // namespace

void PurrCredentials::store(const String &p_key, const String &p_value, bool p_household) {
	if (use_file()) {
		file_store(p_key, p_value);
		return;
	}
	const Char16String name = target(p_key).utf16();
	const CharString value = p_value.utf8();
	CREDENTIALW credential = {};
	credential.Type = CRED_TYPE_GENERIC;
	credential.TargetName = (LPWSTR)name.get_data();
	credential.CredentialBlobSize = value.length();
	credential.CredentialBlob = (LPBYTE)value.get_data();
	credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
	ERR_FAIL_COND_MSG(!CredWriteW(&credential, 0), vformat("Couldn't keep \"%s\" in the Credential Manager (error %d).", p_key, (int)GetLastError()));
}

String PurrCredentials::load(const String &p_key, bool p_household) {
	if (use_file()) {
		return file_load(p_key);
	}
	const Char16String name = target(p_key).utf16();
	PCREDENTIALW credential = nullptr;
	if (!CredReadW((LPCWSTR)name.get_data(), CRED_TYPE_GENERIC, 0, &credential)) {
		return String();
	}
	const String value = String::utf8((const char *)credential->CredentialBlob, credential->CredentialBlobSize);
	CredFree(credential);
	return value;
}

void PurrCredentials::erase(const String &p_key, bool p_household) {
	if (use_file()) {
		file_erase(p_key);
		return;
	}
	const Char16String name = target(p_key).utf16();
	CredDeleteW((LPCWSTR)name.get_data(), CRED_TYPE_GENERIC, 0);
}

#else

void PurrCredentials::store(const String &p_key, const String &p_value, bool p_household) {
	file_store(p_key, p_value);
}

String PurrCredentials::load(const String &p_key, bool p_household) {
	return file_load(p_key);
}

void PurrCredentials::erase(const String &p_key, bool p_household) {
	file_erase(p_key);
}

#endif
