#include "stats.h"

#include "app_watcher.h"
#include "purr.h"

#include "modules/purr_online/purr_online.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "scene/main/scene_tree.h"

Stats *Stats::singleton = nullptr;

namespace {
const char *FILE_PATH = "user://purr/stats.json";
constexpr double SEND_SECONDS = 3 * 60;
// The service's limits (online/src/stats.ts).
constexpr int MAX_CHANGES = 50;
constexpr int MAX_HISTORY_DAYS = 730;

void total_loaded(const Dictionary &p_result, const Ref<RefCounted> &p_call, bool p_history) {
	if (!p_result.get("ok", false)) {
		purr_complete(p_call, purr_failure(p_result.get("error", ""), p_result.get("message", "")));
		return;
	}
	const Dictionary data = p_result.get("data", Dictionary());
	Dictionary result;
	result["ok"] = true;
	if (p_history) {
		result["days"] = data.get("days", Array());
	} else {
		result["total"] = data.get("total", 0.0);
	}
	purr_complete(p_call, result);
}
} // namespace

bool Stats::valid_id(const String &p_id) {
	if (p_id.is_empty() || p_id.length() > 64) {
		return false;
	}
	for (int i = 0; i < p_id.length(); i++) {
		const char32_t c = p_id[i];
		if (!is_ascii_alphanumeric_char(c) && c != '_' && c != '.' && c != '-') {
			return false;
		}
	}
	return true;
}

// The first time the game uses stats: what this device kept, then sending every few minutes.
void Stats::start() {
	if (started || Engine::get_singleton()->is_editor_hint() || SceneTree::get_singleton() == nullptr) {
		return;
	}
	started = true;
	if (FileAccess::exists(FILE_PATH)) {
		const Dictionary kept = JSON::parse_string(FileAccess::get_file_as_string(FILE_PATH));
		values = kept.get("values", Dictionary());
		pending = kept.get("pending", Dictionary());
	}
	PurrAppWatcher::ensure();
	callable_mp(this, &Stats::tick).call_deferred();
}

void Stats::tick() {
	SceneTree::get_singleton()->create_timer(SEND_SECONDS, true, false, true)->connect(SNAME("timeout"), callable_mp(this, &Stats::tick), CONNECT_ONE_SHOT);
	if (!pending.is_empty()) {
		send_now();
	} else if (!fetched) {
		fetch();
	}
	if (steam_changed && Purr::get_singleton()->steam_ready()) {
		Purr::get_singleton()->steam()->call("storeStats");
		steam_changed = false;
	}
}

// The player's values on the service, which may have grown on another device.
void Stats::fetch() {
	if (sending || !Purr::get_singleton()->online_lobbies()) {
		return;
	}
	sending = true;
	PurrOnline::get_singleton()->get_stats().connect(callable_mp(this, &Stats::answered).bind(Dictionary()), CONNECT_ONE_SHOT);
}

void Stats::send_now() {
	if (!started || sending || pending.is_empty() || !Purr::get_singleton()->online_lobbies()) {
		return;
	}
	Dictionary changes;
	Dictionary limits;
	for (const KeyValue<Variant, Variant> &change : pending) {
		if (changes.size() == MAX_CHANGES) {
			break;
		}
		changes[change.key] = change.value;
		const Variant most = ProjectSettings::get_singleton()->get_setting("stats/" + String(change.key) + "/max_per_day", Variant());
		if (most.get_type() == Variant::INT || most.get_type() == Variant::FLOAT || (most.get_type() == Variant::STRING && String(most).is_valid_float())) {
			limits[change.key] = double(most.get_type() == Variant::STRING ? String(most).to_float() : double(most));
		}
	}
	sending = true;
	PurrOnline::get_singleton()->add_stats(changes, limits).connect(callable_mp(this, &Stats::answered).bind(changes), CONNECT_ONE_SHOT);
}

// The service's answer: the player's values, which what's been added since goes on top of. A
// change the service will never take is dropped; one that couldn't get there waits.
void Stats::answered(const Dictionary &p_result, const Dictionary &p_sent) {
	sending = false;
	const int status = p_result.get("status", 0);
	if (!p_result.get("ok", false)) {
		if (status == 400 || status == 403 || status == 404) {
			for (const KeyValue<Variant, Variant> &change : p_sent) {
				pending[change.key] = double(pending.get(change.key, 0.0)) - double(change.value);
				if (double(pending[change.key]) <= 0.0) {
					pending.erase(change.key);
				}
			}
			save_file();
		}
		return;
	}
	for (const KeyValue<Variant, Variant> &change : p_sent) {
		const double left = double(pending.get(change.key, 0.0)) - double(change.value);
		if (left > 0.0) {
			pending[change.key] = left;
		} else {
			pending.erase(change.key);
		}
	}
	const Dictionary before = values;
	values = Dictionary(Dictionary(p_result.get("data", Dictionary())).get("stats", Dictionary())).duplicate();
	for (const KeyValue<Variant, Variant> &change : pending) {
		values[change.key] = double(values.get(change.key, 0.0)) + double(change.value);
	}
	fetched = true;
	save_file();
	for (const KeyValue<Variant, Variant> &value : values) {
		set_steam_stat(value.key);
		// Grown on another device, say.
		if (!Math::is_equal_approx(double(before.get(value.key, 0.0)), double(value.value))) {
			emit_signal(SNAME("changed"), value.key, value.value);
		}
	}
	if (!pending.is_empty()) {
		send_now();
	}
}

void Stats::save_file() const {
	DirAccess::make_dir_recursive_absolute(String(FILE_PATH).get_base_dir());
	Ref<FileAccess> file = FileAccess::open(FILE_PATH, FileAccess::WRITE);
	if (file.is_valid()) {
		Dictionary kept;
		kept["values"] = values;
		kept["pending"] = pending;
		file->store_string(JSON::stringify(kept));
	}
}

// The player's value, to the stat named for it on Steam (an integer stat or a float one).
void Stats::set_steam_stat(const String &p_id) {
	const String name = ProjectSettings::get_singleton()->get_setting("stats/" + p_id + "/steam", String());
	if (name.is_empty() || !Purr::get_singleton()->steam_ready()) {
		return;
	}
	Object *steam = Purr::get_singleton()->steam();
	const double value = values.get(p_id, 0.0);
	const bool set = Math::is_equal_approx(value, Math::round(value)) && bool(steam->call("setStatInt", name, int64_t(Math::round(value))));
	if (!set) {
		steam->call("setStatFloat", name, value);
	}
	steam_changed = true;
}

void Stats::add(const String &p_id, double p_amount) {
	ERR_FAIL_COND_MSG(!valid_id(p_id), vformat("\"%s\" isn't a stat ID: 1 to 64 letters, digits, \"_\", \".\" or \"-\".", p_id));
	ERR_FAIL_COND_MSG(!(p_amount > 0.0) || !Math::is_finite(p_amount), "Stats only go up: add an amount above 0.");
	start();
	values[p_id] = double(values.get(p_id, 0.0)) + p_amount;
	pending[p_id] = double(pending.get(p_id, 0.0)) + p_amount;
	save_file();
	set_steam_stat(p_id);
	emit_signal(SNAME("changed"), p_id, values[p_id]);
}

double Stats::get_value(const String &p_id) {
	start();
	return values.get(p_id, 0.0);
}

Signal Stats::get_total(const String &p_id) {
	Ref<PurrOnlineCall> call;
	call.instantiate();
	if (!Purr::get_singleton()->online_lobbies()) {
		purr_complete(call, purr_failure("offline", "Totals come from the online services, signed in."));
	} else {
		PurrOnline::get_singleton()->get_stat_total(p_id, 0).connect(callable_mp_static(&total_loaded).bind(call, false), CONNECT_ONE_SHOT);
	}
	return Signal(call.ptr(), SNAME("completed"));
}

Signal Stats::get_history(const String &p_id, int p_days) {
	Ref<PurrOnlineCall> call;
	call.instantiate();
	if (!Purr::get_singleton()->online_lobbies()) {
		purr_complete(call, purr_failure("offline", "Totals come from the online services, signed in."));
	} else {
		PurrOnline::get_singleton()->get_stat_total(p_id, CLAMP(p_days, 1, MAX_HISTORY_DAYS)).connect(callable_mp_static(&total_loaded).bind(call, true), CONNECT_ONE_SHOT);
	}
	return Signal(call.ptr(), SNAME("completed"));
}

void Stats::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add", "id", "amount"), &Stats::add);
	ClassDB::bind_method(D_METHOD("get_value", "id"), &Stats::get_value);
	ClassDB::bind_method(D_METHOD("get_total", "id"), &Stats::get_total);
	ClassDB::bind_method(D_METHOD("get_history", "id", "days"), &Stats::get_history);
	ClassDB::bind_method(D_METHOD("send_now"), &Stats::send_now);
	ADD_SIGNAL(MethodInfo("changed", PropertyInfo(Variant::STRING, "id"), PropertyInfo(Variant::FLOAT, "value")));
}

Stats::Stats() {
	singleton = this;
}

Stats::~Stats() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
