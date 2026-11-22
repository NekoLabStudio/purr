#include "register_types.h"

#include "app_store.h"

#include "modules/purr/purr_store.h"

#include "core/object/class_db.h"

void initialize_app_store_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	GDREGISTER_INTERNAL_CLASS(AppStoreBackend);
	Ref<AppStoreBackend> backend;
	backend.instantiate();
	PurrStore::set_backend(backend);
}

void uninitialize_app_store_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	PurrStore::set_backend(Ref<PurrStoreBackend>());
}
