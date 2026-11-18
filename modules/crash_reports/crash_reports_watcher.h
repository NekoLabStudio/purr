#pragma once

// CrashReportsWatcher: tells CrashReports when a phone, tablet or Apple TV sends the game to the
// background and brings it back, which only nodes hear of.

#include "crash_reports.h"

#include "scene/main/node.h"

class CrashReportsWatcher : public Node {
	GDCLASS(CrashReportsWatcher, Node);

protected:
	void _notification(int p_what) {
		CrashReports *reports = CrashReports::get_singleton();
		if (reports != nullptr && (p_what == NOTIFICATION_APPLICATION_PAUSED || p_what == NOTIFICATION_APPLICATION_RESUMED)) {
			reports->set_application_paused(p_what == NOTIFICATION_APPLICATION_PAUSED);
		}
	}
};
