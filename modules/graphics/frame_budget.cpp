#include "frame_budget.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/templates/sort_array.h"
#include "scene/main/scene_tree.h"
#include "servers/display/display_server.h"

FrameBudget *FrameBudget::singleton = nullptr;

namespace {
// A long frame's report names the work that took at least this long, and comes no more than
// once this often (a slow stretch would fill the log).
const uint64_t NAMED_USEC = 1000;
const uint64_t REPORTS_APART_USEC = 1000000;

struct LongerFirst {
	bool operator()(const Pair<String, uint64_t> &p_a, const Pair<String, uint64_t> &p_b) const {
		return p_a.second > p_b.second;
	}
};
} // namespace

FrameBudget::FrameBudget() {
	singleton = this;
	long_frame_ms = GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "frame_budget/long_frame_ms", PROPERTY_HINT_RANGE, "1,1000,1,suffix:ms"), 40.0);
	reporting = GLOBAL_DEF("frame_budget/report_in_release_builds", false);
#ifdef DEBUG_ENABLED
	// In debug builds, unless headless: a frame there draws nothing, so its length says little.
	const DisplayServer *display = DisplayServer::get_singleton();
	reporting = display == nullptr || display->get_name() != "headless";
#endif
	if (!Engine::get_singleton()->is_editor_hint()) {
		callable_mp(this, &FrameBudget::_start).call_deferred();
	}
}

FrameBudget::~FrameBudget() {
	singleton = nullptr;
}

void FrameBudget::_start() {
	SceneTree *tree = SceneTree::get_singleton();
	if (started || tree == nullptr) {
		return;
	}
	started = true;
	tree->connect(SNAME("process_frame"), callable_mp(this, &FrameBudget::_frame));
}

void FrameBudget::measure(const String &p_what, uint64_t p_since_usec) {
	if (reporting) {
		work[p_what] += OS::get_singleton()->get_ticks_usec() - p_since_usec;
	}
}

// Each job steps until its step says it's done (returns false), its object's gone, or this
// frame's share of its budget is spent; the rest waits for the next frame.
void FrameBudget::spread(const String &p_what, const Callable &p_step, double p_budget_ms) {
	ERR_FAIL_COND_MSG(!p_step.is_valid(), "FrameBudget.spread() needs a step to call.");
	ERR_FAIL_COND_MSG(p_budget_ms <= 0.0, "FrameBudget.spread() needs a budget above 0 ms.");
	Job job;
	job.what = p_what;
	job.step = p_step;
	job.budget_ms = p_budget_ms;
	jobs.push_back(job);
	_start();
}

int FrameBudget::get_spread_count() const {
	return jobs.size();
}

void FrameBudget::_run_jobs() {
	const double times = screen_hidden ? HIDDEN_TIMES : 1.0;
	for (int i = 0; i < jobs.size();) {
		const Job job = jobs[i];
		const uint64_t started_usec = OS::get_singleton()->get_ticks_usec();
		const uint64_t budget_usec = uint64_t(job.budget_ms * times * 1000.0);
		bool more = true;
		while (more && job.step.is_valid() && OS::get_singleton()->get_ticks_usec() - started_usec < budget_usec) {
			const Variant result = job.step.call();
			more = result.get_type() == Variant::BOOL && bool(result);
		}
		measure(job.what, started_usec);
		// Work spread by a step joins the end, so this job keeps its place.
		if (more && job.step.is_valid()) {
			i++;
		} else {
			jobs.remove_at(i);
		}
	}
}

void FrameBudget::_frame() {
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	// The frame that just ended, by the clock: the scene tree's delta stops at its physics steps'
	// most, so a 300 ms frame would read as 41.
	const double took_ms = last_frame_usec > 0 ? (now - last_frame_usec) / 1000.0 : 0.0;
	last_frame_usec = now;
	if (reporting && took_ms >= long_frame_ms) {
		Dictionary measured;
		for (const KeyValue<String, uint64_t> &each : work) {
			measured[each.key] = each.value / 1000.0;
		}
		_report(took_ms, measured);
	}
	work.clear();
	_run_jobs();
}

void FrameBudget::_report(double p_ms, const Dictionary &p_work) {
	emit_signal(SNAME("long_frame"), p_ms, p_work);
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	if (last_report_usec > 0 && now - last_report_usec < REPORTS_APART_USEC) {
		return;
	}
	last_report_usec = now;
	Vector<Pair<String, uint64_t>> named;
	for (const KeyValue<String, uint64_t> &each : work) {
		if (each.value >= NAMED_USEC) {
			named.push_back(Pair<String, uint64_t>(each.key, each.value));
		}
	}
	named.sort_custom<LongerFirst>();
	String parts;
	for (const Pair<String, uint64_t> &each : named) {
		parts += (parts.is_empty() ? "" : ", ") + vformat("%s %.1f ms", each.first, each.second / 1000.0);
	}
	print_line(vformat("Long frame: %.0f ms (%s; the rest drawing and the engine's own).", p_ms, parts.is_empty() ? String("nothing measured over 1 ms") : parts));
}

void FrameBudget::set_reporting(bool p_reporting) {
	reporting = p_reporting;
	if (!reporting) {
		work.clear();
	}
}

bool FrameBudget::is_reporting() const {
	return reporting;
}

void FrameBudget::set_long_frame_ms(float p_ms) {
	long_frame_ms = MAX(p_ms, 1.0f);
}

float FrameBudget::get_long_frame_ms() const {
	return long_frame_ms;
}

void FrameBudget::set_screen_hidden(bool p_hidden) {
	screen_hidden = p_hidden;
}

bool FrameBudget::is_screen_hidden() const {
	return screen_hidden;
}

void FrameBudget::_bind_methods() {
	ClassDB::bind_method(D_METHOD("measure", "what", "since_usec"), &FrameBudget::measure);
	ClassDB::bind_method(D_METHOD("spread", "what", "step", "budget_ms"), &FrameBudget::spread, DEFVAL(2.0));
	ClassDB::bind_method(D_METHOD("get_spread_count"), &FrameBudget::get_spread_count);
	ClassDB::bind_method(D_METHOD("set_reporting", "reporting"), &FrameBudget::set_reporting);
	ClassDB::bind_method(D_METHOD("is_reporting"), &FrameBudget::is_reporting);
	ClassDB::bind_method(D_METHOD("set_long_frame_ms", "milliseconds"), &FrameBudget::set_long_frame_ms);
	ClassDB::bind_method(D_METHOD("get_long_frame_ms"), &FrameBudget::get_long_frame_ms);
	ClassDB::bind_method(D_METHOD("set_screen_hidden", "hidden"), &FrameBudget::set_screen_hidden);
	ClassDB::bind_method(D_METHOD("is_screen_hidden"), &FrameBudget::is_screen_hidden);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "reporting"), "set_reporting", "is_reporting");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "long_frame_ms", PROPERTY_HINT_RANGE, "1,1000,1,suffix:ms"), "set_long_frame_ms", "get_long_frame_ms");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "screen_hidden"), "set_screen_hidden", "is_screen_hidden");

	ADD_SIGNAL(MethodInfo("long_frame", PropertyInfo(Variant::FLOAT, "milliseconds"), PropertyInfo(Variant::DICTIONARY, "work")));
}
