#include "crash_reports.h"

#include "crash_report_redaction.h"
#include "crash_reports_watcher.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/debugger/engine_debugger.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/version.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "servers/rendering/rendering_server.h"

#include "modules/options/options.h"
#include "modules/purr_online/purr_online.h"

CrashReports *CrashReports::singleton = nullptr;

#ifdef WINDOWS_ENABLED
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(UNIX_ENABLED)
#include <cerrno>
#include <csignal>
#endif

namespace {
const char *MARKER_PREFIX = "running-";
const char *REPORT_PREFIX = "crash-";
// The player's choice, one of Options' own; off until they agree.
const char *SEND_OPTION = "privacy/send_crash_reports";
// How often to look for the player being signed in, to send what's waiting.
constexpr double SEND_RETRY_SECONDS = 10.0;

void complete_call(const Ref<PurrOnlineCall> &p_call, const Dictionary &p_result) {
	p_call->emit_signal(SNAME("completed"), p_result);
}

// Whether any process with this ID is running. OS::is_process_running() only knows the processes
// the engine started itself, not other copies of the game.
bool process_exists(int64_t p_pid) {
#ifdef WINDOWS_ENABLED
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(p_pid));
	if (process == nullptr) {
		return false;
	}
	DWORD code = 0;
	const bool running = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
	CloseHandle(process);
	return running;
#elif defined(UNIX_ENABLED)
	// Signal 0 checks without sending anything; a process of another user still exists.
	return kill(pid_t(p_pid), 0) == 0 || errno == EPERM;
#else
	return false;
#endif
}
} // namespace

CrashReports::CrashReports() {
	singleton = this;
	enabled = GLOBAL_DEF("crash_reports/enabled", true);
	// Once the game has started: the user folder and the GPU are known then.
	if (!Engine::get_singleton()->is_editor_hint() && !Engine::get_singleton()->is_project_manager_hint()) {
		callable_mp(this, &CrashReports::_start).call_deferred();
	}
}

CrashReports::~CrashReports() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

String CrashReports::_folder() {
	return "user://crashes";
}

void CrashReports::_start() {
	if (!enabled) {
		return;
	}
	DirAccess::make_dir_recursive_absolute(_folder());

	// The markers of runs that have ended without removing them. A marker whose process still runs
	// is another copy of the game, running beside this one.
	for (const String &file : DirAccess::get_files_at(_folder())) {
		if (!file.begins_with(MARKER_PREFIX)) {
			continue;
		}
		const String path = _folder().path_join(file);
		Ref<ConfigFile> marker;
		marker.instantiate();
		if (marker->load(path) != OK) {
			DirAccess::remove_absolute(path);
			continue;
		}
		const int64_t pid = marker->get_value("run", "pid", 0);
		if (pid != 0 && pid != OS::get_singleton()->get_process_id() && process_exists(pid)) {
			continue;
		}
		_report(path);
		DirAccess::remove_absolute(path);
		crashed_before = true;
	}
	_keep_newest(KEPT);
	_send_waiting();
	// As soon as the player agrees, what's waiting goes.
	if (Options::get_singleton() != nullptr) {
		Options::get_singleton()->connect(SNAME("changed"), callable_mp(this, &CrashReports::_option_changed));
	}

	// A run from the editor is stopped by killing it, which would read as a crash every time.
	if (EngineDebugger::is_active()) {
		return;
	}
	watching = true;
	marker_path = _folder().path_join(vformat("%s%d.cfg", MARKER_PREFIX, OS::get_singleton()->get_process_id()));
	_write_marker();
	CrashReportsWatcher *watcher = memnew(CrashReportsWatcher);
	watcher->set_process_mode(Node::PROCESS_MODE_ALWAYS);
	SceneTree::get_singleton()->get_root()->call_deferred(SNAME("add_child"), watcher, false, Node::INTERNAL_MODE_BACK);
}

// The system closes a game in the background without warning, as it needs the memory: that's not
// a crash, so there's no marker while it's there.
void CrashReports::set_application_paused(bool p_paused) {
	if (!watching || paused == p_paused) {
		return;
	}
	paused = p_paused;
	if (paused) {
		DirAccess::remove_absolute(marker_path);
	} else {
		_write_marker();
	}
}

// What this run is running on, written at its start and whenever the game adds context.
void CrashReports::_write_marker() {
	if (!watching || paused) {
		return;
	}
	OS *os = OS::get_singleton();
	RenderingServer *rendering = RenderingServer::get_singleton();
	const Dictionary memory = os->get_memory_info();
	const String gpu = rendering != nullptr ? rendering->get_video_adapter_name() : String();
	const String api = rendering != nullptr ? rendering->get_video_adapter_api_version() : String();

	Ref<ConfigFile> marker;
	marker.instantiate();
	marker->set_value("run", "pid", os->get_process_id());
	marker->set_value("run", "started", Time::get_singleton()->get_datetime_string_from_system(true, true) + " UTC");
	marker->set_value("run", "engine", String(GODOT_VERSION_FULL_NAME));
	marker->set_value("run", "game", vformat("%s %s", String(GLOBAL_GET("application/config/name")), String(GLOBAL_GET("application/config/version"))).strip_edges());
	marker->set_value("run", "os", vformat("%s %s (%s)", os->get_name(), os->get_version(), Engine::get_singleton()->get_architecture_name()));
	marker->set_value("run", "cpu", vformat("%s, %d threads", os->get_processor_name(), os->get_processor_count()));
	marker->set_value("run", "memory", vformat("%d MB", int64_t(memory.get("physical", 0)) / (1024 * 1024)));
	marker->set_value("run", "gpu", gpu.is_empty() ? String("none") : vformat("%s (%s)", gpu, api));
	marker->set_value("run", "context", context);
	marker->save(marker_path);
}

// The last lines of the run's log. Godot copied it aside, under a timestamped name, as this run
// started: the newest copy is the run before this one.
String CrashReports::_log_tail(int p_lines) {
	// Without a log of its own, an older run's log would be taken for it.
	if (!bool(GLOBAL_GET("debug/file_logging/enable_file_logging"))) {
		return String();
	}
	const String log_path = GLOBAL_GET("debug/file_logging/log_path");
	const String folder = log_path.get_base_dir();
	const String base = log_path.get_file().get_basename();
	String newest;
	for (const String &file : DirAccess::get_files_at(folder)) {
		if (file.begins_with(base) && file != log_path.get_file() && file > newest) {
			newest = file;
		}
	}
	if (newest.is_empty()) {
		return String();
	}
	const PackedStringArray lines = FileAccess::get_file_as_string(folder.path_join(newest)).split("\n");
	String tail;
	for (int i = MAX(0, lines.size() - p_lines); i < lines.size(); i++) {
		tail += lines[i] + "\n";
	}
	return vformat("The end of its log (%s):\n\n%s", newest, tail);
}

void CrashReports::_report(const String &p_marker_path) {
	Ref<ConfigFile> marker;
	marker.instantiate();
	marker->load(p_marker_path);
	const String started = marker->get_value("run", "started", "");
	String text = vformat("The run of the game that started %s didn't end cleanly: it crashed, or was closed by force.\n\n", started);
	const char *fields[][2] = { { "engine", "Engine" }, { "game", "Game" }, { "os", "OS" }, { "cpu", "CPU" }, { "memory", "Memory" }, { "gpu", "GPU" } };
	for (const auto &field : fields) {
		const String value = marker->get_value("run", field[0], "");
		if (!value.is_empty()) {
			text += vformat("%s: %s\n", field[1], value);
		}
	}
	const Dictionary game_context = marker->get_value("run", "context", Dictionary());
	for (const KeyValue<Variant, Variant> &entry : game_context) {
		text += vformat("%s: %s\n", String(entry.key), String(entry.value));
	}
	const String tail = _log_tail(LOG_LINES);
	if (!tail.is_empty()) {
		text += "\n" + tail;
	}
	// Down to the millisecond, so names sort in the order the reports were written.
	const double now = Time::get_singleton()->get_unix_time_from_system();
	const String stamp = Time::get_singleton()->get_datetime_string_from_unix_time(int64_t(now)).replace_char(':', '-').replace_char('T', '-') + vformat("-%03d", int(Math::fmod(now, 1.0) * 1000.0));
	String path = _folder().path_join(vformat("%s%s.txt", REPORT_PREFIX, stamp));
	for (int n = 2; FileAccess::exists(path); n++) {
		path = _folder().path_join(vformat("%s%s-%d.txt", REPORT_PREFIX, stamp, n));
	}
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_valid()) {
		file->store_string(text);
	}
}

void CrashReports::_keep_newest(int p_count) {
	const PackedStringArray reports = singleton->get_reports();
	for (int i = p_count; i < reports.size(); i++) {
		DirAccess::remove_absolute(reports[i]);
	}
}

bool CrashReports::previous_run_crashed() const {
	return crashed_before;
}

// Newest first.
PackedStringArray CrashReports::get_reports() const {
	PackedStringArray reports;
	for (const String &file : DirAccess::get_files_at(_folder())) {
		if (file.begins_with(REPORT_PREFIX) && file.ends_with(".txt")) {
			reports.push_back(_folder().path_join(file));
		}
	}
	reports.sort();
	reports.reverse();
	return reports;
}

void CrashReports::delete_report(const String &p_path) {
	ERR_FAIL_COND_MSG(!p_path.begins_with(_folder() + "/") || !p_path.get_file().begins_with(REPORT_PREFIX), vformat("%s isn't a crash report.", p_path));
	DirAccess::remove_absolute(p_path);
}

void CrashReports::set_context(const String &p_key, const Variant &p_value) {
	if (p_value.get_type() == Variant::NIL) {
		context.erase(p_key);
	} else {
		context[p_key] = p_value.stringify();
	}
	_write_marker();
}

Dictionary CrashReports::get_context() const {
	return context.duplicate();
}

// The game's online service takes the report, once the player has agreed and is signed in. A
// report the service has is removed from the device.
Signal CrashReports::send(const String &p_path) {
	if (!_is_report(p_path)) {
		// Completed once the caller is waiting for it; the bound call keeps itself alive till then.
		Ref<PurrOnlineCall> call;
		call.instantiate();
		Dictionary result;
		result["ok"] = false;
		result["error"] = "not_a_report";
		result["message"] = vformat("%s isn't a crash report.", p_path);
		callable_mp_static(&complete_call).bind(call, result).call_deferred();
		return Signal(call.ptr(), SNAME("completed"));
	}
	Signal sent = PurrOnline::get_singleton()->send_crash_report(redact_crash_report(FileAccess::get_file_as_string(p_path)));
	sent.connect(callable_mp(this, &CrashReports::_sent).bind(p_path), CONNECT_ONE_SHOT);
	return sent;
}

bool CrashReports::_is_report(const String &p_path) {
	return p_path.begins_with(_folder() + "/") && p_path.get_file().begins_with(REPORT_PREFIX) && FileAccess::exists(p_path);
}

void CrashReports::_sent(const Dictionary &p_result, const String &p_path) {
	if (bool(p_result.get("ok", false))) {
		DirAccess::remove_absolute(p_path);
	}
}

// With the player's agreement, every report waiting is sent, the oldest first, once they're
// signed in to the online service. Whatever couldn't be sent waits for the next run.
void CrashReports::_send_waiting() {
	if (sending || !_can_send_automatically()) {
		return;
	}
	PackedStringArray reports = get_reports();
	if (reports.is_empty()) {
		return;
	}
	if (PurrOnline::get_singleton()->get_token().is_empty()) {
		SceneTree *tree = SceneTree::get_singleton();
		if (tree != nullptr && !retry_waiting) {
			retry_waiting = true;
			tree->create_timer(SEND_RETRY_SECONDS, true, false, true)->connect(SNAME("timeout"), callable_mp(this, &CrashReports::_retry_send), CONNECT_ONE_SHOT);
		}
		return;
	}
	sending = true;
	const String oldest = reports[reports.size() - 1];
	send(oldest).connect(callable_mp(this, &CrashReports::_sent_waiting), CONNECT_ONE_SHOT);
}

void CrashReports::_retry_send() {
	retry_waiting = false;
	_send_waiting();
}

void CrashReports::_sent_waiting(const Dictionary &p_result) {
	sending = false;
	if (bool(p_result.get("ok", false))) {
		_send_waiting();
	}
}

void CrashReports::_option_changed(const String &p_key, const Variant &p_value) {
	if (p_key == SEND_OPTION && bool(p_value)) {
		_send_waiting();
	}
}

bool CrashReports::_can_send_automatically() const {
	Options *options = Options::get_singleton();
	PurrOnline *online = PurrOnline::get_singleton();
	return enabled && options != nullptr && online != nullptr && online->is_configured() && options->has_option(SEND_OPTION) && bool(options->get_value(SEND_OPTION));
}

void CrashReports::finish() {
	if (watching) {
		DirAccess::remove_absolute(marker_path);
		watching = false;
	}
}

void CrashReports::_bind_methods() {
	ClassDB::bind_method(D_METHOD("previous_run_crashed"), &CrashReports::previous_run_crashed);
	ClassDB::bind_method(D_METHOD("get_reports"), &CrashReports::get_reports);
	ClassDB::bind_method(D_METHOD("delete_report", "path"), &CrashReports::delete_report);
	ClassDB::bind_method(D_METHOD("send", "path"), &CrashReports::send);
	ClassDB::bind_method(D_METHOD("set_context", "key", "value"), &CrashReports::set_context);
	ClassDB::bind_method(D_METHOD("get_context"), &CrashReports::get_context);
}
