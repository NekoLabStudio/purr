#include "player_saves.h"

#include "purr.h"

#include "modules/purr_online/purr_online.h"

#include "core/config/engine.h"
#include "core/io/json.h"
#include "core/io/marshalls.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

namespace {

// Steam Cloud keeps one flat list of files per app; Purr's are named purr_<owner>_<slot>.
String steam_file(const String &p_owner, const String &p_slot) {
	return "purr_" + p_owner + "_" + p_slot;
}

Object *steam() {
	return Engine::get_singleton()->has_singleton("Steam") ? Engine::get_singleton()->get_singleton_object("Steam") : nullptr;
}

Object *cloud_store() {
	return Purr::get_singleton() ? Purr::get_singleton()->cloud_store() : nullptr;
}

} // namespace

void PlayerSaves::setup(const String &p_owner, Backend p_backend, const String &p_token) {
	owner = p_owner.validate_filename();
	backend = p_backend;
	token = p_token;
	cloud = backend == BACKEND_PLATFORM && cloud_store();
	// Saves kept only on this device need a store the system won't clear (tvOS).
	store.setup(owner, backend == BACKEND_LOCAL);
}

bool PlayerSaves::is_valid_slot(const String &p_slot) {
	if (p_slot.is_empty() || p_slot.length() > 64) {
		return false;
	}
	for (int i = 0; i < p_slot.length(); i++) {
		const char32_t c = p_slot[i];
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) {
			return false;
		}
	}
	return true;
}

String PlayerSaves::file_name(const String &p_slot, const String &p_extension) {
	return p_slot.validate_filename() + "." + p_extension;
}

Dictionary PlayerSaves::read_meta(const String &p_slot) const {
	if (!store.exists(file_name(p_slot, "json"))) {
		return Dictionary();
	}
	const PackedByteArray bytes = store.read(file_name(p_slot, "json"));
	const Variant parsed = JSON::parse_string(String::utf8((const char *)bytes.ptr(), bytes.size()));
	return parsed.get_type() == Variant::DICTIONARY ? Dictionary(parsed) : Dictionary();
}

void PlayerSaves::write_meta(const String &p_slot, const Dictionary &p_meta) const {
	ERR_FAIL_COND_MSG(!store.write(file_name(p_slot, "json"), JSON::stringify(p_meta).to_utf8_buffer()), "Couldn't write a save's details for slot " + p_slot + ".");
}

PackedByteArray PlayerSaves::read_local(const String &p_slot) const {
	return store.read(file_name(p_slot, "save"));
}

void PlayerSaves::write_local(const String &p_slot, const PackedByteArray &p_data) const {
	ERR_FAIL_COND_MSG(!store.write(file_name(p_slot, "save"), p_data), "Couldn't write the save for slot " + p_slot + ".");
}

void PlayerSaves::save(const String &p_slot, const Variant &p_data, const Dictionary &p_summary) {
	ERR_FAIL_COND_MSG(!is_valid_slot(p_slot), vformat("\"%s\" isn't a slot name saves can sync under: use 1 to 64 lowercase letters, digits, \"-\" or \"_\".", p_slot));
	PackedByteArray data;
	int length = 0;
	ERR_FAIL_COND(encode_variant(p_data, nullptr, length, false) != OK);
	data.resize(length);
	encode_variant(p_data, data.ptrw(), length, false);
	write_local(p_slot, data);

	Dictionary meta = read_meta(p_slot);
	meta["dirty"] = true;
	meta["edits"] = (int64_t)meta.get("edits", 0) + 1;
	meta["summary"] = p_summary;
	write_meta(p_slot, meta);

	if (backend == BACKEND_PLATFORM && cloud) {
		cloud_upload(p_slot);
	} else if (backend == BACKEND_PLATFORM && steam()) {
		steam_write(p_slot);
	} else if (backend == BACKEND_ONLINE) {
		upload(p_slot);
	}
}

// Writes a slot to Steam Cloud. Only a write Steam accepts makes it synced: a failed one (the
// cloud full, say) stays dirty and is tried again at the next sync, and until then the local save
// is never replaced by Steam's older copy.
void PlayerSaves::steam_write(const String &p_slot) {
	const PackedByteArray data = read_local(p_slot);
	if (!(bool)steam()->call("fileWrite", steam_file(owner, p_slot), data, data.size())) {
		WARN_PRINT(vformat("Steam Cloud didn't take the save in slot %s (is the game's cloud quota full?). It's kept on this device and tried again later.", p_slot));
		return;
	}
	Dictionary meta = read_meta(p_slot);
	meta["dirty"] = false;
	write_meta(p_slot, meta);
	emit_signal(SNAME("synced"), p_slot);
}

// Each achievement's furthest progress (0 to 1) in either list.
Dictionary PlayerSaves::merge_achievements(const Variant &p_a, const Variant &p_b) {
	Dictionary merged = p_a.get_type() == Variant::DICTIONARY ? Dictionary(p_a).duplicate() : Dictionary();
	if (p_b.get_type() != Variant::DICTIONARY) {
		return merged;
	}
	for (const KeyValue<Variant, Variant> &entry : Dictionary(p_b)) {
		if ((double)entry.value > (double)merged.get(entry.key, 0.0)) {
			merged[entry.key] = entry.value;
		}
	}
	return merged;
}

// Each mode's rating from the side with more matches, and every block from either.
Dictionary PlayerSaves::merge_multiplayer(const Variant &p_a, const Variant &p_b) {
	Dictionary merged = p_a.get_type() == Variant::DICTIONARY ? Dictionary(p_a).duplicate(true) : Dictionary();
	if (p_b.get_type() != Variant::DICTIONARY) {
		return merged;
	}
	const Dictionary other = p_b;
	Dictionary ratings = merged.get("ratings", Dictionary());
	for (const KeyValue<Variant, Variant> &entry : Dictionary(other.get("ratings", Dictionary()))) {
		const Dictionary theirs = entry.value;
		const Dictionary ours = ratings.get(entry.key, Dictionary());
		if ((int64_t)theirs.get("matches", 0) > (int64_t)ours.get("matches", 0)) {
			ratings[entry.key] = theirs.duplicate();
		}
	}
	merged["ratings"] = ratings;
	Dictionary blocked = merged.get("blocked", Dictionary());
	blocked.merge(Dictionary(other.get("blocked", Dictionary())));
	merged["blocked"] = blocked;
	return merged;
}

bool PlayerSaves::merges(const String &p_slot) {
	return p_slot == PURR_ACHIEVEMENTS_SLOT || p_slot == PURR_MULTIPLAYER_SLOT;
}

Variant PlayerSaves::merge(const String &p_slot, const Variant &p_a, const Variant &p_b) {
	return p_slot == PURR_MULTIPLAYER_SLOT ? merge_multiplayer(p_a, p_b) : merge_achievements(p_a, p_b);
}

// Purr's slots never conflict: changed here and on another device, the two sides are merged and
// the result goes up over the other device's (p_key is "version" for the online service,
// "modified" for a cloud store).
void PlayerSaves::merge_remote(const String &p_slot, const PackedByteArray &p_remote, const String &p_key, int64_t p_remote_version) {
	Variant remote;
	if (decode_variant(remote, p_remote.ptr(), p_remote.size(), nullptr, false) != OK) {
		remote = Variant();
	}
	Dictionary meta = read_meta(p_slot);
	meta[p_key] = p_remote_version;
	meta.erase("conflict_version");
	write_meta(p_slot, meta);
	save(p_slot, merge(p_slot, load(p_slot), remote), Dictionary());
	emit_signal(SNAME("synced"), p_slot);
}

// A download was asked for when the slot had p_edits edits; if the player has saved since, the
// other device's save is offered against this one rather than replacing it.
bool PlayerSaves::saved_meanwhile(const String &p_slot, int64_t p_edits, int64_t p_remote, const Dictionary &p_remote_summary) {
	Dictionary meta = read_meta(p_slot);
	if ((int64_t)meta.get("edits", 0) == p_edits) {
		return false;
	}
	meta["conflict_version"] = p_remote;
	write_meta(p_slot, meta);
	emit_signal(SNAME("conflict"), p_slot, meta.get("summary", Dictionary()), p_remote_summary);
	return true;
}

Variant PlayerSaves::load(const String &p_slot) const {
	const PackedByteArray data = read_local(p_slot);
	if (data.is_empty()) {
		return Variant();
	}
	Variant value;
	ERR_FAIL_COND_V_MSG(decode_variant(value, data.ptr(), data.size(), nullptr, false) != OK, Variant(), "The save in slot " + p_slot + " is damaged.");
	return value;
}

bool PlayerSaves::has(const String &p_slot) const {
	return store.exists(file_name(p_slot, "save"));
}

Dictionary PlayerSaves::get_summary(const String &p_slot) const {
	return read_meta(p_slot).get("summary", Dictionary());
}

void PlayerSaves::upload(const String &p_slot) {
	if (uploading.has(p_slot) || !PurrOnline::get_singleton() || PurrOnline::get_singleton()->get_token().is_empty()) {
		return; // Synced later: on the next save or sign-in.
	}
	const Dictionary meta = read_meta(p_slot);
	const int64_t version = meta.get("version", 0);
	uploading.insert(p_slot);
	Signal done = PurrOnline::get_singleton()->write_save(p_slot, read_local(p_slot), version, meta.get("summary", Dictionary()), token);
	done.connect(callable_mp(this, &PlayerSaves::uploaded).bind(p_slot, meta.get("edits", 0)), CONNECT_ONE_SHOT);
}

void PlayerSaves::uploaded(const Dictionary &p_result, const String &p_slot, int64_t p_edits_sent) {
	uploading.erase(p_slot);
	Dictionary meta = read_meta(p_slot);
	if (p_result.get("ok", false)) {
		meta["version"] = Dictionary(p_result["data"]).get("version", 0);
		// A save made while this one was on its way stays dirty, and goes up now.
		const bool changed_meanwhile = (int64_t)meta.get("edits", 0) != p_edits_sent;
		meta["dirty"] = changed_meanwhile;
		write_meta(p_slot, meta);
		if (changed_meanwhile) {
			upload(p_slot);
		} else {
			emit_signal(SNAME("synced"), p_slot);
		}
	} else if (p_result.get("error", "") == "version_conflict") {
		// Another device saved since: offer both, with the other one's summary.
		Signal listed_signal = PurrOnline::get_singleton()->list_saves(token);
		listed_signal.connect(callable_mp(this, &PlayerSaves::listed), CONNECT_ONE_SHOT);
	} else if ((int)p_result.get("status", 0) >= 400 && (int)p_result.get("status", 0) < 500 && (int)p_result.get("status", 0) != 429) {
		// Refused, not just unreachable: trying again won't help until the game changes something
		// (a save too big, too many slots), so say why. Still dirty, kept on this device.
		ERR_PRINT(vformat("The online services refused the save in slot %s: %s (%s)", p_slot, String(p_result.get("message", "")), String(p_result.get("error", ""))));
	}
}

void PlayerSaves::sync() {
	if (backend == BACKEND_PLATFORM && cloud) {
		Signal listed_signal = cloud_store()->call("list_files");
		listed_signal.connect(callable_mp(this, &PlayerSaves::cloud_listed), CONNECT_ONE_SHOT);
	} else if (backend == BACKEND_PLATFORM && steam()) {
		// Steam Cloud has already brought down the latest at launch: refresh the local copies
		// that have no changes of their own.
		const int count = steam()->call("getFileCount");
		for (int i = 0; i < count; i++) {
			const Dictionary file = steam()->call("getFileNameAndSize", i);
			const String name = file.get("name", "");
			const String prefix = steam_file(owner, "");
			if (!name.begins_with(prefix)) {
				continue;
			}
			const String slot = name.substr(prefix.length());
			if (read_meta(slot).get("dirty", false)) {
				continue;
			}
			const Dictionary read = steam()->call("fileRead", name, file.get("size", 0));
			if (read.get("ret", false)) {
				write_local(slot, read["buf"]);
			}
		}
		// Saves Steam didn't take last time go up now.
		for (const String &slot : get_slots()) {
			if (read_meta(slot).get("dirty", false)) {
				steam_write(slot);
			}
		}
	} else if (backend == BACKEND_ONLINE && PurrOnline::get_singleton() && !PurrOnline::get_singleton()->get_token().is_empty()) {
		Signal listed_signal = PurrOnline::get_singleton()->list_saves(token);
		listed_signal.connect(callable_mp(this, &PlayerSaves::listed), CONNECT_ONE_SHOT);
	}
}

void PlayerSaves::listed(const Dictionary &p_result) {
	if (!p_result.get("ok", false)) {
		return;
	}
	const Array remote = Dictionary(p_result["data"]).get("saves", Array());
	HashSet<String> remote_slots;
	for (const Variant &entry : remote) {
		const Dictionary save = entry;
		const String slot = save.get("slot", "");
		const int64_t remote_version = save.get("version", 0);
		remote_slots.insert(slot);
		Dictionary meta = read_meta(slot);
		const int64_t local_version = meta.get("version", 0);
		const bool dirty = meta.get("dirty", false);
		if (remote_version > local_version && !dirty) {
			Signal read = PurrOnline::get_singleton()->read_save(slot, token);
			read.connect(callable_mp(this, &PlayerSaves::downloaded).bind(slot, remote_version, meta.get("edits", 0)), CONNECT_ONE_SHOT);
		} else if (remote_version > local_version && dirty && merges(slot)) {
			Signal read = PurrOnline::get_singleton()->read_save(slot, token);
			read.connect(callable_mp(this, &PlayerSaves::downloaded).bind(slot, remote_version, meta.get("edits", 0)), CONNECT_ONE_SHOT);
		} else if (remote_version > local_version && dirty) {
			meta["conflict_version"] = remote_version;
			write_meta(slot, meta);
			emit_signal(SNAME("conflict"), slot, meta.get("summary", Dictionary()), save.get("summary", Dictionary()));
		} else if (dirty) {
			upload(slot);
		}
	}
	// Saves made on this device that the service has never seen.
	for (const String &file : store.list()) {
		if (file.ends_with(".json") && !remote_slots.has(file.get_basename()) && read_meta(file.get_basename()).get("dirty", false)) {
			upload(file.get_basename());
		}
	}
}

void PlayerSaves::downloaded(const Dictionary &p_result, const String &p_slot, int64_t p_version, int64_t p_edits) {
	if (!p_result.get("ok", false)) {
		return;
	}
	if (merges(p_slot) && read_meta(p_slot).get("dirty", false)) {
		merge_remote(p_slot, p_result["data"], "version", p_version);
		return;
	}
	const Dictionary headers = p_result.get("headers", Dictionary());
	const Variant summary = JSON::parse_string(headers.get("x-save-summary", "null"));
	if (saved_meanwhile(p_slot, p_edits, p_version, summary.get_type() == Variant::DICTIONARY ? Dictionary(summary) : Dictionary())) {
		return;
	}
	write_local(p_slot, p_result["data"]);
	Dictionary meta = read_meta(p_slot);
	meta.erase("conflict_version");
	meta["version"] = p_version;
	meta["dirty"] = false;
	meta["summary"] = summary;
	write_meta(p_slot, meta);
	emit_signal(SNAME("synced"), p_slot);
}

void PlayerSaves::resolve_conflict(const String &p_slot, bool p_keep_local) {
	Dictionary meta = read_meta(p_slot);
	ERR_FAIL_COND_MSG(!meta.has("conflict_version"), "There's no conflict to resolve in slot " + p_slot + ".");
	const int64_t remote_version = meta["conflict_version"];
	meta.erase("conflict_version");
	if (cloud) {
		// A cloud store: this device's save goes up over the other, or the other comes down.
		meta["modified"] = remote_version;
		meta["dirty"] = p_keep_local;
		write_meta(p_slot, meta);
		if (p_keep_local) {
			cloud_upload(p_slot);
		} else {
			cloud_download(p_slot);
		}
		return;
	}
	if (p_keep_local) {
		// Uploaded over the other device's save, which the service keeps as a backup.
		meta["version"] = remote_version;
		meta["dirty"] = true;
		write_meta(p_slot, meta);
		upload(p_slot);
	} else {
		meta["dirty"] = false;
		write_meta(p_slot, meta);
		Signal read = PurrOnline::get_singleton()->read_save(p_slot, token);
		read.connect(callable_mp(this, &PlayerSaves::downloaded).bind(p_slot, remote_version, meta.get("edits", 0)), CONNECT_ONE_SHOT);
	}
}

// A cloud store: each file is a save, named like Steam Cloud's files, with the save's summary as
// its metadata. "modified" in a save's details is the file's time on the store's servers when this
// device last had the same save, so a newer one there means another device saved.

void PlayerSaves::cloud_upload(const String &p_slot) {
	if (uploading.has(p_slot)) {
		return; // Goes up when this upload is done (it's still dirty).
	}
	const Dictionary meta = read_meta(p_slot);
	Dictionary file_details;
	file_details["summary"] = meta.get("summary", Dictionary());
	uploading.insert(p_slot);
	Signal done = cloud_store()->call("save_file", steam_file(owner, p_slot), read_local(p_slot), file_details);
	done.connect(callable_mp(this, &PlayerSaves::cloud_uploaded).bind(p_slot, meta.get("edits", 0)), CONNECT_ONE_SHOT);
}

void PlayerSaves::cloud_uploaded(const Dictionary &p_result, const String &p_slot, int64_t p_edits_sent) {
	uploading.erase(p_slot);
	if (!p_result.get("ok", false)) {
		return; // Still dirty: goes up on the next save or sync.
	}
	Dictionary meta = read_meta(p_slot);
	meta["modified"] = p_result.get("modified", 0);
	const bool changed_meanwhile = (int64_t)meta.get("edits", 0) != p_edits_sent;
	meta["dirty"] = changed_meanwhile;
	write_meta(p_slot, meta);
	if (changed_meanwhile) {
		cloud_upload(p_slot);
	} else {
		emit_signal(SNAME("synced"), p_slot);
	}
}

void PlayerSaves::cloud_listed(const Dictionary &p_result) {
	if (!p_result.get("ok", false)) {
		return;
	}
	const String prefix = steam_file(owner, "");
	HashSet<String> remote_slots;
	for (const Variant &entry : Array(p_result.get("files", Array()))) {
		const Dictionary file = entry;
		const String name = file.get("name", "");
		if (!name.begins_with(prefix)) {
			continue;
		}
		const String slot = name.substr(prefix.length());
		const int64_t remote = file.get("modified", 0);
		remote_slots.insert(slot);
		Dictionary meta = read_meta(slot);
		const bool dirty = meta.get("dirty", false);
		if (remote > (int64_t)meta.get("modified", 0) && !dirty) {
			cloud_download(slot);
		} else if (remote > (int64_t)meta.get("modified", 0) && dirty && merges(slot)) {
			cloud_download(slot);
		} else if (remote > (int64_t)meta.get("modified", 0) && dirty) {
			meta["conflict_version"] = remote;
			write_meta(slot, meta);
			emit_signal(SNAME("conflict"), slot, meta.get("summary", Dictionary()), Dictionary(file.get("metadata", Dictionary())).get("summary", Dictionary()));
		} else if (dirty) {
			cloud_upload(slot);
		}
	}
	for (const String &slot : get_slots()) {
		if (!remote_slots.has(slot) && read_meta(slot).get("dirty", false)) {
			cloud_upload(slot);
		}
	}
}

void PlayerSaves::cloud_download(const String &p_slot) {
	Signal loaded = cloud_store()->call("load_file", steam_file(owner, p_slot));
	loaded.connect(callable_mp(this, &PlayerSaves::cloud_downloaded).bind(p_slot, read_meta(p_slot).get("edits", 0)), CONNECT_ONE_SHOT);
}

void PlayerSaves::cloud_downloaded(const Dictionary &p_result, const String &p_slot, int64_t p_edits) {
	if (!p_result.get("ok", false)) {
		return;
	}
	if (merges(p_slot) && read_meta(p_slot).get("dirty", false)) {
		merge_remote(p_slot, p_result["data"], "modified", p_result.get("modified", 0));
		return;
	}
	const Dictionary remote_summary = Dictionary(p_result.get("metadata", Dictionary())).get("summary", Dictionary());
	if (saved_meanwhile(p_slot, p_edits, p_result.get("modified", 0), remote_summary)) {
		return;
	}
	write_local(p_slot, p_result["data"]);
	Dictionary meta = read_meta(p_slot);
	meta.erase("conflict_version");
	meta["modified"] = p_result.get("modified", 0);
	meta["dirty"] = false;
	meta["summary"] = Dictionary(p_result.get("metadata", Dictionary())).get("summary", Dictionary());
	write_meta(p_slot, meta);
	emit_signal(SNAME("synced"), p_slot);
}

PackedStringArray PlayerSaves::get_slots() const {
	PackedStringArray slots;
	for (const String &file : store.list()) {
		if (file.ends_with(".save")) {
			slots.push_back(file.get_basename());
		}
	}
	return slots;
}

PackedStringArray PlayerSaves::get_game_slots() const {
	PackedStringArray slots = get_slots();
	slots.erase(PURR_ACHIEVEMENTS_SLOT);
	slots.erase(PURR_MULTIPLAYER_SLOT);
	return slots;
}

// One slot of another player's saves as this device's own, not yet synced with this backend: the
// backend compares it with what it has.
void PlayerSaves::take_slot(const Ref<PlayerSaves> &p_from, const String &p_slot) {
	Dictionary meta = p_from->read_meta(p_slot);
	if (p_from->owner != owner) {
		write_local(p_slot, p_from->read_local(p_slot));
	}
	meta.erase("version");
	meta.erase("modified");
	meta.erase("conflict_version");
	meta["dirty"] = true;
	meta["edits"] = (int64_t)meta.get("edits", 0) + 1;
	write_meta(p_slot, meta);
}

void PlayerSaves::adopt(const Ref<PlayerSaves> &p_from) {
	ERR_FAIL_COND(p_from.is_null());
	for (const String &slot : p_from->get_slots()) {
		take_slot(p_from, slot);
	}
	sync();
}

void PlayerSaves::carry_over(const Ref<PlayerSaves> &p_from) {
	ERR_FAIL_COND(p_from.is_null() || p_from->owner == owner);
	for (const String &slot : p_from->get_slots()) {
		if (merges(slot)) {
			save(slot, merge(slot, load(slot), p_from->load(slot)), Dictionary());
		} else if (has(slot) || (backend == BACKEND_PLATFORM && !cloud && steam() && (bool)steam()->call("fileExists", steam_file(owner, slot)))) {
			// This player already has a save in that slot (here, or in Steam Cloud, which can't
			// offer both): it's kept, and so is the other one, where it was.
			WARN_PRINT(vformat("A save in slot %s from before this account was known on this device wasn't carried over: the account already has one there.", slot));
			continue;
		} else {
			// The online service and iCloud or Play Games offer both when they already have one.
			take_slot(p_from, slot);
		}
		p_from->delete_slot(slot);
	}
}

bool PlayerSaves::has_unsynced() const {
	for (const String &slot : get_slots()) {
		if (read_meta(slot).get("dirty", false)) {
			return true;
		}
	}
	return false;
}

void PlayerSaves::delete_all() {
	for (const String &slot : get_slots()) {
		delete_slot(slot);
	}
	store.remove_all();
}

void PlayerSaves::delete_slot(const String &p_slot) {
	if (backend == BACKEND_PLATFORM && cloud) {
		cloud_store()->call("delete_file", steam_file(owner, p_slot));
	} else if (backend == BACKEND_PLATFORM && steam()) {
		steam()->call("fileDelete", steam_file(owner, p_slot));
	}
	store.remove(file_name(p_slot, "save"));
	store.remove(file_name(p_slot, "json"));
}

void PlayerSaves::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_backend"), &PlayerSaves::get_backend);
	ClassDB::bind_method(D_METHOD("save", "slot", "data", "summary"), &PlayerSaves::save, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("get_slots"), &PlayerSaves::get_game_slots);
	ClassDB::bind_method(D_METHOD("load", "slot"), &PlayerSaves::load);
	ClassDB::bind_method(D_METHOD("has", "slot"), &PlayerSaves::has);
	ClassDB::bind_method(D_METHOD("get_summary", "slot"), &PlayerSaves::get_summary);
	ClassDB::bind_method(D_METHOD("sync"), &PlayerSaves::sync);
	ClassDB::bind_method(D_METHOD("resolve_conflict", "slot", "keep_local"), &PlayerSaves::resolve_conflict);

	ADD_SIGNAL(MethodInfo("synced", PropertyInfo(Variant::STRING, "slot")));
	ADD_SIGNAL(MethodInfo("conflict", PropertyInfo(Variant::STRING, "slot"), PropertyInfo(Variant::DICTIONARY, "local_summary"), PropertyInfo(Variant::DICTIONARY, "remote_summary")));

	BIND_ENUM_CONSTANT(BACKEND_LOCAL);
	BIND_ENUM_CONSTANT(BACKEND_PLATFORM);
	BIND_ENUM_CONSTANT(BACKEND_ONLINE);
}
