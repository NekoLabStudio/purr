#include "crash_report_redaction.h"

#include "core/os/os.h"
#include "core/string/regex.h"

namespace {
// The same as the online service's (Trill's src/crash_reports.ts).
// Every repeat is bounded, so no text can make these slow.
const char *EMAIL = R"re([\w.+-]{1,64}@[\w-]{1,63}(?:\.[\w-]{1,63}){1,8})re";
const char *IPV4 = R"re((?<![\w.])(?:(?:25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(?:25[0-5]|2[0-4]\d|1?\d?\d)(?![\w.]))re";
// Candidates only: one is an IPv6 address if it has a hex digit and "::" or three colons, so a
// time (19:22:25) or a C++ name (Node::_ready) stays.
const char *IPV6 = R"re((?<![\w:])[0-9a-fA-F:]{2,45}(?:%\w{1,16})?(?![\w:]))re";
// A home folder named for the player, wherever it is: a Mac's or Linux's short name has no
// spaces; a Windows one may.
const char *HOME = R"re((?m)(/Users/|/home/)[^/\s]{1,128}|[A-Za-z]:\\Users\\[^\\/\n]{1,128}?(?=\\|$))re";

bool is_ipv6(const String &p_text) {
	bool hex = false;
	for (int i = 0; i < p_text.length() && !hex; i++) {
		hex = is_hex_digit(p_text[i]);
	}
	return hex && (p_text.contains("::") || p_text.get_slice_count(":") >= 4);
}
} // namespace

String redact_crash_report(const String &p_report) {
	String text = p_report;
	// The home folder by its own path first, then by its usual shape.
	const String home = OS::get_singleton()->get_environment(OS::get_singleton()->has_environment("USERPROFILE") ? "USERPROFILE" : "HOME");
	if (home.length() > 3) {
		text = text.replace(home, "~");
	}
	text = RegEx::create_from_string(HOME)->sub(text, "~", true);
	text = RegEx::create_from_string(EMAIL)->sub(text, "[email]", true);
	text = RegEx::create_from_string(IPV4)->sub(text, "[address]", true);

	String out;
	int from = 0;
	const TypedArray<RegExMatch> candidates = RegEx::create_from_string(IPV6)->search_all(text);
	for (int i = 0; i < candidates.size(); i++) {
		const Ref<RegExMatch> match = candidates[i];
		if (!is_ipv6(match->get_string(0))) {
			continue;
		}
		out += text.substr(from, match->get_start(0) - from) + "[address]";
		from = match->get_end(0);
	}
	return out + text.substr(from);
}
