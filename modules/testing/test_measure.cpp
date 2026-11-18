#include "test_measure.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"

namespace {
// A change smaller than this is noise between runs.
constexpr double SAME = 0.15;

String ms(double p_ms) {
	return String::num(p_ms, p_ms < 1.0 ? 3 : 2) + " ms";
}
} // namespace

String TestMeasure::_path() {
	return "user://test_measures.json";
}

void TestMeasure::start(const String &p_what, const String &p_script, const Callable &p_step, int p_frames) {
	what = p_what;
	key = p_script + " " + p_what;
	step = p_step;
	frames_left = MAX(p_frames, 1);
	SceneTree::get_singleton()->connect(SNAME("process_frame"), callable_mp(this, &TestMeasure::_frame));
}

void TestMeasure::_frame() {
	if (!step.is_valid()) {
		_finish();
		return;
	}
	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	step.call();
	times_usec.push_back(OS::get_singleton()->get_ticks_usec() - started);
	frames_left--;
	if (frames_left <= 0) {
		_finish();
	}
}

void TestMeasure::_finish() {
	SceneTree::get_singleton()->disconnect(SNAME("process_frame"), callable_mp(this, &TestMeasure::_frame));
	finished = true;
	Dictionary result;
	if (times_usec.is_empty()) {
		ERR_PRINT(vformat("Nothing to measure for \"%s\": its step's object is gone.", what));
		call_deferred(SNAME("emit_signal"), SNAME("done"), result);
		return;
	}
	Vector<uint64_t> sorted = times_usec;
	sorted.sort();
	// The middle call: one slow call (the first, a hiccup of the computer's) doesn't move it.
	const double median = (sorted[(sorted.size() - 1) / 2] + sorted[sorted.size() / 2]) / 2000.0;
	const double p95 = sorted[MAX(int(Math::ceil(sorted.size() * 0.95)) - 1, 0)] / 1000.0;
	const double worst = sorted[sorted.size() - 1] / 1000.0;
	result["median_ms"] = median;
	result["p95_ms"] = p95;
	result["worst_ms"] = worst;

	// Against the last run on this computer, and kept for the next.
	Dictionary runs;
	if (FileAccess::exists(_path())) {
		const Variant read = JSON::parse_string(FileAccess::get_file_as_string(_path()));
		if (read.get_type() == Variant::DICTIONARY) {
			runs = read;
		}
	}
	String against;
	if (runs.has(key) && double(runs[key]) > 0.0) {
		const double before = runs[key];
		result["last_run_ms"] = before;
		const double change = (median - before) / before;
		if (Math::abs(change) < SAME) {
			against = vformat(" (about the same as last run's %s)", ms(before));
		} else {
			against = vformat(" (%d%% %s than last run's %s)", int(Math::round(Math::abs(change) * 100.0)), change > 0.0 ? "slower" : "faster", ms(before));
		}
	}
	runs[key] = median;
	Ref<FileAccess> file = FileAccess::open(_path(), FileAccess::WRITE);
	if (file.is_valid()) {
		file->store_string(JSON::stringify(runs, "\t", true));
	}

	print_line(vformat("TIME  %s: %s a call, 95%% within %s, slowest %s%s", key, ms(median), ms(p95), ms(worst), against));
	call_deferred(SNAME("emit_signal"), SNAME("done"), result);
}

void TestMeasure::_bind_methods() {
	ADD_SIGNAL(MethodInfo("done", PropertyInfo(Variant::DICTIONARY, "result")));
}
