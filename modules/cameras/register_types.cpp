#include "register_types.h"

#include "camera_director.h"
#include "virtual_camera_2d.h"
#include "virtual_camera_3d.h"

#include "core/object/class_db.h"

void initialize_cameras_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(VirtualCamera3D);
		GDREGISTER_CLASS(VirtualCamera2D);
		GDREGISTER_INTERNAL_CLASS(CameraDirector);
	}
}

void uninitialize_cameras_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		CameraDirector::cleanup();
	}
}
