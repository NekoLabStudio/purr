#include "register_types.h"

#include "project_check.h"
#include "test_case.h"
#include "test_runner.h"
#include "test_wait.h"
#include "testing_startup.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"

void initialize_testing_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(TestCase);
		GDREGISTER_INTERNAL_CLASS(TestWait);
		GDREGISTER_INTERNAL_CLASS(TestRunner);
		GDREGISTER_INTERNAL_CLASS(ProjectCheck);
		GLOBAL_DEF(PropertyInfo(Variant::STRING, "testing/tests_folder", PROPERTY_HINT_DIR), "res://tests");
		GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "testing/timeout_seconds", PROPERTY_HINT_RANGE, "0.1,600,0.1,or_greater,suffix:s"), 30.0);
		GLOBAL_DEF(PropertyInfo(Variant::PACKED_STRING_ARRAY, "testing/check_excludes", PROPERTY_HINT_TYPE_STRING, vformat("%d/%d:", Variant::STRING, PROPERTY_HINT_DIR)), PackedStringArray({ "res://addons" }));
		TestingStartup::take_over_if_asked();
	}
}

void uninitialize_testing_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		TestingStartup::release();
	}
}
