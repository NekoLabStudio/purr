#include "register_types.h"

#include "icloud.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

static ICloud *icloud = nullptr;

void initialize_icloud_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_CLASS(ICloud);
	GDREGISTER_INTERNAL_CLASS(ICloudCall);
	icloud = memnew(ICloud);
	Engine::get_singleton()->add_singleton(Engine::Singleton("ICloud", ICloud::get_singleton()));
}

void uninitialize_icloud_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	memdelete(icloud);
}
