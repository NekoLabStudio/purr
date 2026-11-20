#include "register_types.h"

#include "camera_director.h"
#include "photo_mode.h"
#include "photo_mode_view.h"
#include "virtual_camera_2d.h"
#include "virtual_camera_3d.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

static PhotoMode *photo_mode = nullptr;

void initialize_cameras_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(VirtualCamera3D);
		GDREGISTER_CLASS(VirtualCamera2D);
		GDREGISTER_INTERNAL_CLASS(CameraDirector);
		GDREGISTER_CLASS(PhotoMode);
		GDREGISTER_INTERNAL_CLASS(PhotoModeView);
		GDREGISTER_INTERNAL_CLASS(PhotoModeShot);
		photo_mode = memnew(PhotoMode);
		Engine::get_singleton()->add_singleton(Engine::Singleton("PhotoMode", PhotoMode::get_singleton()));
	}
}

void uninitialize_cameras_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		CameraDirector::cleanup();
		memdelete(photo_mode);
		photo_mode = nullptr;
	}
}
