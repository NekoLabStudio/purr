#pragma once

// A WebRTC data channel backed by libdatachannel. Based on WebRTCLibDataChannel from
// godotengine/webrtc-native (MIT, Copyright (c) 2018 Godot Engine).

#include "modules/webrtc/webrtc_data_channel.h"

#include <rtc/rtc.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <utility>
#include <vector>

class WebRTCNativeDataChannel : public WebRTCDataChannel {
	GDCLASS(WebRTCNativeDataChannel, WebRTCDataChannel);

	using QueuedPacket = std::pair<std::vector<uint8_t>, bool>; // The data, and whether it was text.

	// What libdatachannel's threads hand over; they hold it weakly, so it can outlive neither side.
	struct SharedState {
		std::mutex mutex;
		std::queue<QueuedPacket> packets;
		std::atomic<uint32_t> available_packets = 0;
		std::atomic<ChannelState> channel_state = STATE_CONNECTING;
	};

	std::shared_ptr<SharedState> shared_state = std::make_shared<SharedState>();
	std::shared_ptr<rtc::DataChannel> channel;
	QueuedPacket current_packet;
	WriteMode write_mode = WRITE_MODE_BINARY;
	bool negotiated = false;

protected:
	static void _bind_methods() {}

public:
	static Ref<WebRTCNativeDataChannel> wrap(const std::shared_ptr<rtc::DataChannel> &p_channel, bool p_negotiated);

	// PacketPeer
	int get_available_packet_count() const override;
	Error get_packet(const uint8_t **r_buffer, int &r_buffer_size) override;
	Error put_packet(const uint8_t *p_buffer, int p_buffer_size) override;
	int get_max_packet_size() const override;

	// WebRTCDataChannel
	void set_write_mode(WriteMode p_mode) override;
	WriteMode get_write_mode() const override;
	bool was_string_packet() const override;

	ChannelState get_ready_state() const override;
	String get_label() const override;
	bool is_ordered() const override;
	int get_id() const override;
	int get_max_packet_life_time() const override;
	int get_max_retransmits() const override;
	String get_protocol() const override;
	bool is_negotiated() const override;
	int get_buffered_amount() const override;

	Error poll() override;
	void close() override;

	~WebRTCNativeDataChannel();
};
