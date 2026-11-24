#include "feedback.h"

#include "purr.h"

#include "modules/crash_reports/crash_reports.h"
#include "modules/purr_online/purr_online.h"

#include "core/config/project_settings.h"
#include "core/object/callable_mp.h"

Feedback *Feedback::singleton = nullptr;

namespace {
bool valid_category(const String &p_category) {
	if (p_category.is_empty() || p_category.length() > 64) {
		return false;
	}
	for (int i = 0; i < p_category.length(); i++) {
		const char32_t c = p_category[i];
		if (!is_ascii_lower_case(c) && !is_digit(c) && c != '_' && c != '-') {
			return false;
		}
	}
	return true;
}

void sent(const Dictionary &p_result, const Ref<RefCounted> &p_call) {
	if (p_result.get("ok", false)) {
		Dictionary result;
		result["ok"] = true;
		purr_complete(p_call, result);
	} else {
		purr_complete(p_call, purr_failure(p_result.get("error", ""), p_result.get("message", "")));
	}
}
} // namespace

bool Feedback::is_available() const {
	return Purr::get_singleton()->online_lobbies();
}

Signal Feedback::send(const String &p_category, const String &p_text, bool p_include_log) {
	Ref<PurrOnlineCall> call;
	call.instantiate();
	const Signal completed(call.ptr(), SNAME("completed"));
	const String text = p_text.strip_edges();
	if (!valid_category(p_category)) {
		purr_complete(call, purr_failure("bad_request", "A category is 1 to 64 lowercase letters, digits, \"-\" or \"_\"."));
		return completed;
	}
	if (text.is_empty() || text.length() > MAX_TEXT) {
		purr_complete(call, purr_failure("bad_request", vformat("Feedback is 1 to %d characters.", MAX_TEXT)));
		return completed;
	}
	if (!is_available()) {
		purr_complete(call, purr_failure("not_signed_in", "Feedback goes to the online services: sign in to them first."));
		return completed;
	}
	const Dictionary context = CrashReports::get_singleton() ? CrashReports::get_singleton()->get_context() : Dictionary();
	String details = CrashReports::describe_run(CrashReports::get_run_details(), context);
	if (p_include_log) {
		const String tail = CrashReports::get_log_tail(CrashReports::LOG_LINES, true);
		if (!tail.is_empty()) {
			details += "\n" + tail;
		}
	}
	const bool named = ProjectSettings::get_singleton()->get_setting("feedback/" + p_category + "/named", false);
	Signal sending = PurrOnline::get_singleton()->send_feedback(p_category, text, named, details);
	sending.connect(callable_mp_static(&sent).bind(call), CONNECT_ONE_SHOT);
	return completed;
}

void Feedback::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_available"), &Feedback::is_available);
	ClassDB::bind_method(D_METHOD("send", "category", "text", "include_log"), &Feedback::send, DEFVAL(false));
}

Feedback::Feedback() {
	singleton = this;
}

Feedback::~Feedback() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
