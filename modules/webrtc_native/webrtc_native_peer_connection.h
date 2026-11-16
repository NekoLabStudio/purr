#pragma once

// WebRTCPeerConnection backed by libdatachannel, the engine's default on every platform but the
// web (where the browser's WebRTC is used). Based on WebRTCLibPeerConnection from
// godotengine/webrtc-native (MIT, Copyright (c) 2018 Godot Engine).
//
// It extends WebRTCPeerConnectionExtension because that is what WebRTCPeerConnection creates by
// default; a GDExtension that sets its own default still takes over.

#include "modules/webrtc/webrtc_peer_connection_extension.h"

#include <rtc/rtc.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <queue>

class WebRTCNativePeerConnection : public WebRTCPeerConnectionExtension {
	GDCLASS(WebRTCNativePeerConnection, WebRTCPeerConnectionExtension);

	// Signals raised on libdatachannel's threads wait here until poll() emits them.
	struct SharedState {
		std::mutex mutex;
		std::queue<std::function<void(Object *)>> tasks;
	};

	std::shared_ptr<rtc::PeerConnection> peer_connection;
	std::shared_ptr<SharedState> shared_state = std::make_shared<SharedState>();

	Error create_peer_connection(rtc::Configuration &r_config);

protected:
	static void _bind_methods() {}

public:
	static void initialize_logging();
	static void cleanup();

	ConnectionState get_connection_state() const override;
	GatheringState get_gathering_state() const override;
	SignalingState get_signaling_state() const override;

	Error initialize(const Dictionary &p_config) override;
	Ref<WebRTCDataChannel> create_data_channel(const String &p_label, const Dictionary &p_options) override;
	Error create_offer() override;
	Error set_remote_description(const String &p_type, const String &p_sdp) override;
	Error set_local_description(const String &p_type, const String &p_sdp) override;
	Error add_ice_candidate(const String &p_sdp_mid_name, int p_sdp_mline_index_name, const String &p_sdp_name) override;
	Error poll() override;
	void close() override;

	WebRTCNativePeerConnection();
	~WebRTCNativePeerConnection();
};
