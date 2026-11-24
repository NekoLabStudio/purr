#include "live_config.h"

#include "app_watcher.h"
#include "purr.h"

#include "modules/purr_online/purr_online.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "scene/main/scene_tree.h"
#include "scene/main/timer.h"

LiveConfig *LiveConfig::singleton = nullptr;

namespace {
const char *CACHE_PATH = "user://purr/live_config.json";
constexpr double REFRESH_SECONDS = 15 * 60;
// Coming back to the front refreshes, but no more often than this.
constexpr uint64_t RESUME_REFRESH_MSEC = 60 * 1000;
// The longest a timer waits for a scheduled switch; it looks again after that.
constexpr int64_t LONGEST_WAIT = 24 * 3600;

int64_t device_time() {
	return (int64_t)OS::get_singleton()->get_unix_time();
}

int64_t optional_time(const Dictionary &p_value, const String &p_key) {
	const Variant time = p_value.get(p_key, Variant());
	return time.get_type() == Variant::NIL ? -1 : (int64_t)time;
}
} // namespace


Variant LiveConfig::get_value(const String &p_key, const Variant &p_default) {
	start();
	if (in_effect.has(p_key)) {
		return in_effect[p_key];
	}
	const String setting = "live_config/" + p_key;
	if (ProjectSettings::get_singleton()->has_setting(setting)) {
		return ProjectSettings::get_singleton()->get_setting(setting);
	}
	return p_default;
}

bool LiveConfig::is_loaded() {
	start();
	return loaded;
}

// The first time the game asks: what an earlier run kept, then the service's, then every so often.
// Only in a running game, and only one that uses live config, so others never ask the service.
void LiveConfig::start() {
	if (started || Engine::get_singleton()->is_editor_hint() || SceneTree::get_singleton() == nullptr) {
		return;
	}
	started = true;
	const String cached = FileAccess::exists(CACHE_PATH) ? FileAccess::get_file_as_string(CACHE_PATH) : String();
	if (!cached.is_empty()) {
		const Dictionary kept = JSON::parse_string(cached);
		use(kept.get("values", Array()), kept.get("clock_offset", 0));
	}
	if (!PurrOnline::get_singleton()->is_configured()) {
		return;
	}
	refresh();
	PurrAppWatcher::ensure();
	refresh_every();
}

void LiveConfig::refresh_every() {
	SceneTree::get_singleton()->create_timer(REFRESH_SECONDS, true, false, true)->connect(SNAME("timeout"), callable_mp(this, &LiveConfig::refresh_every), CONNECT_ONE_SHOT);
	if (Time::get_singleton()->get_ticks_msec() - last_refresh_msec >= REFRESH_SECONDS * 1000 - 1000) {
		refresh();
	}
}

void LiveConfig::resumed() {
	if (started && PurrOnline::get_singleton()->is_configured() && Time::get_singleton()->get_ticks_msec() - last_refresh_msec >= RESUME_REFRESH_MSEC) {
		refresh();
	}
}

Signal LiveConfig::refresh() {
	start();
	if (refreshing.is_valid()) {
		return Signal(refreshing.ptr(), SNAME("completed"));
	}
	Ref<PurrOnlineCall> call;
	call.instantiate();
	if (!PurrOnline::get_singleton()->is_configured()) {
		purr_complete(call, purr_failure("not_configured", "Live config comes from the online services (online/url)."));
		return Signal(call.ptr(), SNAME("completed"));
	}
	refreshing = call;
	last_refresh_msec = Time::get_singleton()->get_ticks_msec();
	Signal loaded_signal = PurrOnline::get_singleton()->get_live_config();
	loaded_signal.connect(callable_mp(this, &LiveConfig::refreshed).bind(call), CONNECT_ONE_SHOT);
	return Signal(call.ptr(), SNAME("completed"));
}

void LiveConfig::refreshed(const Dictionary &p_result, const Ref<RefCounted> &p_call) {
	refreshing.unref();
	if (!p_result.get("ok", false)) {
		purr_complete(p_call, purr_failure(p_result.get("error", ""), p_result.get("message", "")));
		return;
	}
	const Dictionary data = p_result.get("data", Dictionary());
	const Array service_values = data.get("values", Array());
	// Seconds are enough: a schedule switches within a second of its time.
	const int64_t offset = (int64_t)data.get("time", device_time()) - device_time();
	use(service_values, offset);
	DirAccess::make_dir_recursive_absolute(String(CACHE_PATH).get_base_dir());
	Ref<FileAccess> file = FileAccess::open(CACHE_PATH, FileAccess::WRITE);
	if (file.is_valid()) {
		Dictionary kept;
		kept["values"] = service_values;
		kept["clock_offset"] = offset;
		file->store_string(JSON::stringify(kept));
	}
	Dictionary result;
	result["ok"] = true;
	purr_complete(p_call, result);
}

void LiveConfig::use(const Array &p_values, int64_t p_clock_offset) {
	values = p_values;
	clock_offset = p_clock_offset;
	loaded = true;
	update_in_effect();
}

int64_t LiveConfig::service_time() const {
	return device_time() + clock_offset;
}

// Each key's value now: of those in effect, the one that started last. Tells the game when any
// changed, and waits for the next scheduled switch.
void LiveConfig::update_in_effect() {
	const int64_t now = service_time();
	Dictionary now_in_effect;
	Dictionary started_at;
	for (const Variant &entry : values) {
		const Dictionary value = entry;
		const int64_t starts = optional_time(value, "starts_at");
		const int64_t ends = optional_time(value, "ends_at");
		if ((starts >= 0 && starts > now) || (ends >= 0 && ends <= now)) {
			continue;
		}
		const String key = value.get("key", "");
		if (!now_in_effect.has(key) || (int64_t)started_at[key] <= starts) {
			now_in_effect[key] = value.get("value", Variant());
			started_at[key] = starts;
		}
	}
	const bool changed = !now_in_effect.recursive_equal(in_effect, 0);
	in_effect = now_in_effect;
	schedule_switch();
	if (changed) {
		emit_signal(SNAME("changed"));
	}
}

void LiveConfig::schedule_switch() {
	schedule_generation++;
	if (!started || SceneTree::get_singleton() == nullptr) {
		return;
	}
	const int64_t now = service_time();
	int64_t next = -1;
	for (const Variant &entry : values) {
		const Dictionary value = entry;
		for (const int64_t time : { optional_time(value, "starts_at"), optional_time(value, "ends_at") }) {
			if (time > now && (next < 0 || time < next)) {
				next = time;
			}
		}
	}
	if (next < 0) {
		return;
	}
	const double wait = MIN(next - now, LONGEST_WAIT) + 0.5;
	SceneTree::get_singleton()->create_timer(wait, true, false, true)->connect(SNAME("timeout"), callable_mp(this, &LiveConfig::switch_due).bind(schedule_generation), CONNECT_ONE_SHOT);
}

void LiveConfig::switch_due(uint64_t p_generation) {
	if (p_generation == schedule_generation) {
		update_in_effect();
	}
}

void LiveConfig::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_value", "key", "default"), &LiveConfig::get_value, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("is_loaded"), &LiveConfig::is_loaded);
	ClassDB::bind_method(D_METHOD("refresh"), &LiveConfig::refresh);
	ADD_SIGNAL(MethodInfo("changed"));
}

LiveConfig::LiveConfig() {
	singleton = this;
}

LiveConfig::~LiveConfig() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
