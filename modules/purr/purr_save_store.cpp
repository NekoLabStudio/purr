#include "purr_save_store.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"

#ifdef TVOS_ENABLED
#include <CoreFoundation/CoreFoundation.h>

namespace {
// tvOS keeps about 500 KB of preferences per app: warn before saves run into it.
const int64_t PREFERENCES_WARNING_BYTES = 400 * 1024;

CFStringRef preferences_key(const String &p_owner) {
	return CFStringCreateWithCString(kCFAllocatorDefault, ("purr.saves." + p_owner).utf8().get_data(), kCFStringEncodingUTF8);
}
} // namespace
#endif

void PurrSaveStore::setup(const String &p_owner, bool p_no_other_copy) {
	owner = p_owner;
#ifdef TVOS_ENABLED
	preferences = p_no_other_copy;
#else
	(void)p_no_other_copy;
#endif
}

String PurrSaveStore::folder() const {
	return "user://purr/saves/" + owner;
}

// The owner's files in the preferences: one dictionary of name to bytes.
Dictionary PurrSaveStore::read_preferences() const {
	Dictionary files;
#ifdef TVOS_ENABLED
	CFStringRef key = preferences_key(owner);
	CFPropertyListRef value = CFPreferencesCopyAppValue(key, kCFPreferencesCurrentApplication);
	CFRelease(key);
	if (value == nullptr) {
		return files;
	}
	if (CFGetTypeID(value) == CFDictionaryGetTypeID()) {
		CFDictionaryRef dictionary = (CFDictionaryRef)value;
		CFIndex count = CFDictionaryGetCount(dictionary);
		LocalVector<const void *> keys;
		LocalVector<const void *> values;
		keys.resize(count);
		values.resize(count);
		CFDictionaryGetKeysAndValues(dictionary, keys.ptr(), values.ptr());
		for (CFIndex i = 0; i < count; i++) {
			if (CFGetTypeID(keys[i]) != CFStringGetTypeID() || CFGetTypeID(values[i]) != CFDataGetTypeID()) {
				continue;
			}
			char name[256];
			if (!CFStringGetCString((CFStringRef)keys[i], name, sizeof(name), kCFStringEncodingUTF8)) {
				continue;
			}
			CFDataRef data = (CFDataRef)values[i];
			PackedByteArray bytes;
			bytes.resize(CFDataGetLength(data));
			if (!bytes.is_empty()) {
				CFDataGetBytes(data, CFRangeMake(0, bytes.size()), bytes.ptrw());
			}
			files[String::utf8(name)] = bytes;
		}
	}
	CFRelease(value);
#endif
	return files;
}

void PurrSaveStore::write_preferences(const Dictionary &p_files) const {
#ifdef TVOS_ENABLED
	CFMutableDictionaryRef dictionary = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	int64_t total = 0;
	for (const KeyValue<Variant, Variant> &entry : p_files) {
		const PackedByteArray bytes = entry.value;
		total += bytes.size();
		CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, String(entry.key).utf8().get_data(), kCFStringEncodingUTF8);
		CFDataRef data = CFDataCreate(kCFAllocatorDefault, bytes.ptr(), bytes.size());
		CFDictionarySetValue(dictionary, name, data);
		CFRelease(name);
		CFRelease(data);
	}
	CFStringRef key = preferences_key(owner);
	CFPreferencesSetAppValue(key, p_files.is_empty() ? nullptr : dictionary, kCFPreferencesCurrentApplication);
	CFRelease(key);
	CFRelease(dictionary);
	CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
	if (total > PREFERENCES_WARNING_BYTES) {
		WARN_PRINT_ONCE(vformat("This player's saves take %d KB of the about 500 KB tvOS keeps for an app's local saves. Keep saves small, or add iCloud or Purr's online services for room.", total / 1024));
	}
#else
	(void)p_files;
#endif
}

PackedByteArray PurrSaveStore::read(const String &p_name) const {
	if (preferences) {
		return read_preferences().get(p_name, PackedByteArray());
	}
	// Nothing there yet (an empty slot, a save never synced) isn't an error: nothing is returned.
	const String path = folder().path_join(p_name);
	return FileAccess::exists(path) ? FileAccess::get_file_as_bytes(path) : PackedByteArray();
}

bool PurrSaveStore::write(const String &p_name, const PackedByteArray &p_data) const {
	if (preferences) {
		Dictionary files = read_preferences();
		files[p_name] = p_data;
		write_preferences(files);
		return true;
	}
	// The folder is made by the first save, so looking at a player's saves leaves nothing behind.
	DirAccess::make_dir_recursive_absolute(folder());
	// Written beside it first and moved over it, so a crash mid-write never leaves half a file.
	const String final_path = folder().path_join(p_name);
	const String temporary = final_path + ".tmp";
	Ref<FileAccess> file = FileAccess::open(temporary, FileAccess::WRITE);
	if (file.is_null()) {
		return false;
	}
	file->store_buffer(p_data);
	file->close();
	return DirAccess::rename_absolute(temporary, final_path) == OK;
}

bool PurrSaveStore::exists(const String &p_name) const {
	if (preferences) {
		return read_preferences().has(p_name);
	}
	return FileAccess::exists(folder().path_join(p_name));
}

void PurrSaveStore::remove(const String &p_name) const {
	if (preferences) {
		Dictionary files = read_preferences();
		files.erase(p_name);
		write_preferences(files);
		return;
	}
	DirAccess::remove_absolute(folder().path_join(p_name));
}

PackedStringArray PurrSaveStore::list() const {
	PackedStringArray names;
	if (preferences) {
		for (const Variant &name : read_preferences().keys()) {
			names.push_back(name);
		}
		return names;
	}
	Ref<DirAccess> dir = DirAccess::open(folder());
	if (dir.is_valid()) {
		names = dir->get_files();
	}
	return names;
}

void PurrSaveStore::remove_all() const {
	if (preferences) {
		write_preferences(Dictionary());
		return;
	}
	for (const String &name : list()) {
		DirAccess::remove_absolute(folder().path_join(name));
	}
	DirAccess::remove_absolute(folder());
}
