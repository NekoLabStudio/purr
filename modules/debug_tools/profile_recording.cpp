#include "profile_recording.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "scene/main/scene_tree.h"

#include "modules/graphics/frame_budget.h"

namespace {
String ms(double p_ms) {
	return String::num(p_ms, p_ms < 10.0 ? 2 : 1) + " ms";
}

// GDScript names a function "res://player.gd::42::jump": shown as "res://player.gd:42 jump".
String function_name(const String &p_signature) {
	const PackedStringArray parts = p_signature.split("::");
	if (parts.size() == 3) {
		return vformat("%s:%s %s", parts[0], parts[1], parts[2]);
	}
	return p_signature;
}

struct MoreSelfTime {
	bool operator()(const ScriptLanguage::ProfilingInfo &p_a, const ScriptLanguage::ProfilingInfo &p_b) const {
		return p_a.self_time > p_b.self_time;
	}
};
} // namespace

ProfileRecording::~ProfileRecording() {
	stop();
}

double ProfileRecording::get_seconds_left() const {
	return recording ? MAX(seconds - (OS::get_singleton()->get_ticks_usec() - started_usec) / 1000000.0, 0.0) : 0.0;
}

void ProfileRecording::start(double p_seconds) {
	SceneTree *tree = SceneTree::get_singleton();
	ERR_FAIL_NULL(tree);
	if (recording) {
		return;
	}
	recording = true;
	seconds = p_seconds;
	frame_ms.clear();
	longest.clear();
	started_usec = OS::get_singleton()->get_ticks_usec();
	last_frame_usec = started_usec;
	tree->connect(SNAME("process_frame"), callable_mp(this, &ProfileRecording::_frame));
	if (FrameBudget::get_singleton() != nullptr) {
		FrameBudget::get_singleton()->connect(SNAME("long_frame"), callable_mp(this, &ProfileRecording::_long_frame));
	}
	for (int i = 0; i < ScriptServer::get_language_count(); i++) {
		ScriptServer::get_language(i)->profiling_set_save_native_calls(true);
		ScriptServer::get_language(i)->profiling_start();
	}
}

void ProfileRecording::stop() {
	if (!recording) {
		return;
	}
	recording = false;
	for (int i = 0; i < ScriptServer::get_language_count(); i++) {
		ScriptServer::get_language(i)->profiling_stop();
	}
	if (SceneTree::get_singleton() != nullptr) {
		SceneTree::get_singleton()->disconnect(SNAME("process_frame"), callable_mp(this, &ProfileRecording::_frame));
	}
	if (FrameBudget::get_singleton() != nullptr) {
		FrameBudget::get_singleton()->disconnect(SNAME("long_frame"), callable_mp(this, &ProfileRecording::_long_frame));
	}
}

void ProfileRecording::_frame() {
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	frame_ms.push_back((now - last_frame_usec) / 1000.0f);
	last_frame_usec = now;
	if (now - started_usec >= uint64_t(seconds * 1000000.0)) {
		_finish();
	}
}

void ProfileRecording::_long_frame(double p_ms, const Dictionary &p_work) {
	int at = 0;
	while (at < longest.size() && longest[at].first >= p_ms) {
		at++;
	}
	if (at < LONGEST_KEPT) {
		longest.insert(at, Pair<double, Dictionary>(p_ms, p_work));
		if (longest.size() > LONGEST_KEPT) {
			longest.resize(LONGEST_KEPT);
		}
	}
}

void ProfileRecording::_finish() {
	// The script functions' times before profiling stops.
	Vector<ScriptLanguage::ProfilingInfo> functions;
	functions.resize(65536);
	int count = 0;
	for (int i = 0; i < ScriptServer::get_language_count(); i++) {
		count += ScriptServer::get_language(i)->profiling_get_accumulated_data(functions.ptrw() + count, functions.size() - count);
	}
	functions.resize(count);
	stop();

	const double took = (OS::get_singleton()->get_ticks_usec() - started_usec) / 1000000.0;
	Vector<float> sorted = frame_ms;
	sorted.sort();
	const int frames = sorted.size();
	const double median = frames > 0 ? sorted[(frames - 1) / 2] : 0.0;
	const double p95 = frames > 0 ? sorted[MAX(int(Math::ceil(frames * 0.95)) - 1, 0)] : 0.0;
	const double slowest = frames > 0 ? sorted[frames - 1] : 0.0;
	const double long_ms = FrameBudget::get_singleton() != nullptr ? FrameBudget::get_singleton()->get_long_frame_ms() : 40.0;
	int long_frames = 0;
	for (float frame : sorted) {
		long_frames += frame >= long_ms ? 1 : 0;
	}

	const String game = vformat("%s %s", String(GLOBAL_GET("application/config/name")), String(GLOBAL_GET("application/config/version"))).strip_edges();
	String summary = vformat("Frames: %d in %.0f s, %s at the middle, 95%% within %s, slowest %s; %d took %s or more.\n", frames, took, ms(median), ms(p95), ms(slowest), long_frames, ms(long_ms));

	String report = vformat("Profile of %s, %s UTC, for %.0f s.\n\n", game, Time::get_singleton()->get_datetime_string_from_system(true, true), took) + summary;
	if (!longest.is_empty()) {
		report += "\nThe longest frames, with the work FrameBudget measured in them:\n";
		for (const Pair<double, Dictionary> &frame : longest) {
			String work;
			for (const KeyValue<Variant, Variant> &each : frame.second) {
				work += (work.is_empty() ? "" : ", ") + vformat("%s %s", String(each.key), ms(each.value));
			}
			report += vformat("  %s: %s\n", ms(frame.first), work.is_empty() ? String("nothing measured") : work);
		}
	}

	String top;
	if (count == 0) {
		report += "\nScript functions aren't timed in release builds.\n";
	} else {
		functions.sort_custom<MoreSelfTime>();
		report += "\nScript functions by their own time (not counting what they call), each frame on average:\n";
		const double per_frame = MAX(frames, 1);
		for (int i = 0; i < MIN(count, FUNCTIONS_SHOWN) && functions[i].self_time > 0; i++) {
			const ScriptLanguage::ProfilingInfo &function = functions[i];
			const String line = vformat("  %s  %s (%d calls)", ms(function.self_time / 1000.0 / per_frame), function_name(function.signature), int64_t(function.call_count));
			report += line + "\n";
			if (i < 5) {
				top += line + "\n";
			}
		}
	}

	const String folder = "user://profiles";
	DirAccess::make_dir_recursive_absolute(folder);
	const String path = folder.path_join("profile-" + Time::get_singleton()->get_datetime_string_from_system().replace_char(':', '-').replace_char('T', '-') + ".txt");
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_valid()) {
		file->store_string(report);
	}
	print_line(summary + (top.is_empty() ? "" : "The costliest script functions, each frame:\n" + top) + "The whole report: " + ProjectSettings::get_singleton()->globalize_path(path));
}
