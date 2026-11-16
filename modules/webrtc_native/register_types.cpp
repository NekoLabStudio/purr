#include "register_types.h"

#include "webrtc_native_data_channel.h"
#include "webrtc_native_peer_connection.h"

#include "core/object/class_db.h"

void initialize_webrtc_native_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	WebRTCNativePeerConnection::initialize_logging();
	GDREGISTER_INTERNAL_CLASS(WebRTCNativeDataChannel);
	GDREGISTER_CLASS(WebRTCNativePeerConnection);
	WebRTCPeerConnection::set_default_extension(WebRTCNativePeerConnection::get_class_static());
}

void uninitialize_webrtc_native_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	WebRTCNativePeerConnection::cleanup();
}
