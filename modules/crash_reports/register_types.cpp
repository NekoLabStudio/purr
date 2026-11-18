#include "register_types.h"

#include "crash_reports.h"
#include "crash_reports_watcher.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

static CrashReports *crash_reports = nullptr;

void initialize_crash_reports_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(CrashReports);
		GDREGISTER_INTERNAL_CLASS(CrashReportsWatcher);
		crash_reports = memnew(CrashReports);
		Engine::get_singleton()->add_singleton(Engine::Singleton("CrashReports", CrashReports::get_singleton()));
	}
}

void uninitialize_crash_reports_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		// Reached only by a run that ends cleanly: its marker goes.
		crash_reports->finish();
		memdelete(crash_reports);
		crash_reports = nullptr;
	}
}
