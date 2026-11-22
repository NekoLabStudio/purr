#pragma once

// WebRTCPeerConnection backed by libdatachannel, the engine's default on every platform but the
// web (where the browser's WebRTC is used). Based on WebRTCLibPeerConnection from
// godotengine/webrtc-native (MIT, Copyright (c) 2018 Godot Engine).
//
// It extends WebRTCPeerConnectionExtension because that is what WebRTCPeerConnection creates by
// default; a GDExtension that sets its own default still takes over.
//
// renew_ice_servers() moves a live connection onto new ICE servers (TURN credentials that are
// about to end, renewed): a second connection is made with them, negotiated over a control channel
// on the first (CONTROL_CHANNEL_ID, made on every connection), its data channels take over (see
// WebRTCNativeDataChannel), and the first closes. The game sees one connection throughout. Both
// sides must be this class: elsewhere the move times out and the first connection carries on.

#include "webrtc_native_data_channel.h"

#include "modules/webrtc/webrtc_peer_connection_extension.h"

#include <rtc/rtc.hpp>

#include <functional>
#include <string>
#include <vector>
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

	// A data channel the game has, and how to make it again on another connection.
	struct ChannelRecord {
		Ref<WebRTCNativeDataChannel> wrapper;
		std::string label;
		rtc::DataChannelInit init;
	};

	// A move to another connection under way.
	struct Move {
		std::shared_ptr<rtc::PeerConnection> connection;
		std::shared_ptr<rtc::DataChannel> control;
		std::vector<std::shared_ptr<rtc::DataChannel>> channels; // One for each record, in order.
		std::string marker; // Sent last on the old connection's ordered channels.
		std::string remote_marker;
		uint64_t nonce = 0; // Names the move's messages, and settles two moves started at once.
		uint64_t remote_nonce = 0; // The other side's, from its offer or answer.
		bool initiator = false;
		bool moved = false; // This side sends on the new connection.
		bool remote_moved = false; // The other side does.
		uint64_t started_msec = 0;
		uint64_t settled_msec = 0;
	};

	static constexpr uint16_t CONTROL_CHANNEL_ID = 1023;
	static constexpr uint64_t MOVE_TIMEOUT_MSEC = 30000;
	static constexpr uint64_t MOVE_SETTLE_MSEC = 2000;

	std::shared_ptr<rtc::PeerConnection> peer_connection;
	std::shared_ptr<rtc::DataChannel> control;
	std::shared_ptr<SharedState> shared_state = std::make_shared<SharedState>();
	rtc::Configuration configuration; // What this connection was made with, ICE servers renewed.
	std::vector<ChannelRecord> records;
	std::unique_ptr<Move> move;
	bool move_waiting = false; // renew_ice_servers() during a move: another follows it.

	Error create_peer_connection(rtc::Configuration &r_config);
	std::function<void(std::function<void(Object *)>)> task_queue();
	void open_control(const std::shared_ptr<rtc::PeerConnection> &p_connection, std::shared_ptr<rtc::DataChannel> &r_control);
	void record(const Ref<WebRTCNativeDataChannel> &p_wrapper);
	Error start_move(bool p_initiator, const std::string &p_remote_marker);
	void send_control(const Dictionary &p_message);
	void control_received(const String &p_text);
	void advance_move();
	void complete_move();
	void cancel_move();

protected:
	static void _bind_methods();

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

	Error renew_ice_servers(const Array &p_ice_servers);
	bool uses_relay();
	bool is_renewing() const { return move != nullptr; }

	WebRTCNativePeerConnection();
	~WebRTCNativePeerConnection();
};
