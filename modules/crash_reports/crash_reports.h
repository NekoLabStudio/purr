#pragma once

// CrashReports: a report for every run of the game that didn't end cleanly. Each run leaves a
// marker in user://crashes saying what it was running on, and deletes it when it ends cleanly. On
// the next start, a marker whose process has gone is a run that crashed or was killed: its report
// (versions, system, the game's own context, the end of its log) is written beside it. Nothing
// leaves the device unless the player agrees (privacy/send_crash_reports, in Options): then each
// is sent to the game's online service, without email or IP addresses, and removed once it's there.

#include "core/object/object.h"
#include "core/variant/dictionary.h"

class CrashReports : public Object {
	GDCLASS(CrashReports, Object);

	static CrashReports *singleton;

	bool enabled = true;
	bool watching = false; // This run has a marker.
	bool paused = false; // In the background, where the system may close it at any time.
	bool crashed_before = false;
	bool sending = false;
	bool retry_waiting = false; // Looking again later for the player being signed in.
	Dictionary context;
	String marker_path;

	static String _folder();
	void _start();
	void _write_marker();
	void _report(const String &p_marker_path);
	static void _keep_newest(int p_count);
	static bool _is_report(const String &p_path);
	void _sent(const Dictionary &p_result, const String &p_path);
	void _send_waiting();
	void _retry_send();
	void _sent_waiting(const Dictionary &p_result);
	void _option_changed(const String &p_key, const Variant &p_value);
	bool _can_send_automatically() const;

protected:
	static void _bind_methods();

public:
	static CrashReports *get_singleton() { return singleton; }

	static constexpr int KEPT = 20;
	static constexpr int LOG_LINES = 200;

	// What the game runs on, for a report (Feedback's too): {"engine", "game", "os", "cpu",
	// "memory", "gpu"}, and those as a report's lines with the game's context after them.
	static Dictionary get_run_details();
	static String describe_run(const Dictionary &p_details, const Dictionary &p_context);
	// The last lines of a run's log: this one's, or the one before (the crashed run, as this one
	// starts).
	static String get_log_tail(int p_lines, bool p_this_run);

	// From CrashReportsWatcher, as the system sends the game to the background and back.
	void set_application_paused(bool p_paused);

	bool previous_run_crashed() const;
	PackedStringArray get_reports() const;
	void delete_report(const String &p_path);
	Signal send(const String &p_path);
	void set_context(const String &p_key, const Variant &p_value);
	Dictionary get_context() const;

	void finish();

	CrashReports();
	~CrashReports();
};
