#include "app_watcher.h"

#include "live_config.h"
#include "stats.h"

#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

bool PurrAppWatcher::added = false;

void PurrAppWatcher::ensure() {
	SceneTree *tree = SceneTree::get_singleton();
	if (added || tree == nullptr || tree->get_root() == nullptr) {
		return;
	}
	added = true;
	PurrAppWatcher *watcher = memnew(PurrAppWatcher);
	watcher->set_process_mode(PROCESS_MODE_ALWAYS);
	tree->get_root()->call_deferred(SNAME("add_child"), watcher, false, INTERNAL_MODE_BACK);
}

void PurrAppWatcher::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_APPLICATION_RESUMED:
		case NOTIFICATION_APPLICATION_FOCUS_IN:
			LiveConfig::get_singleton()->resumed();
			break;
		case NOTIFICATION_APPLICATION_PAUSED:
		case NOTIFICATION_APPLICATION_FOCUS_OUT:
		case NOTIFICATION_WM_CLOSE_REQUEST:
			Stats::get_singleton()->send_now();
			break;
		case NOTIFICATION_PREDELETE:
			added = false;
			break;
	}
}
