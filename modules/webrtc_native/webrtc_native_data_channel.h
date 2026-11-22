#pragma once

// A WebRTC data channel backed by libdatachannel. Based on WebRTCLibDataChannel from
// godotengine/webrtc-native (MIT, Copyright (c) 2018 Godot Engine).
//
// Its connection can move to another (WebRTCNativePeerConnection::renew_ice_servers) without the
// game seeing it: the channel is bound to the new connection's channel of the same ID beside the
// old one, sends switch over once it's open, and on a channel that's ordered and reliable, what
// arrives on the new one waits until the old one's last message (the other side's marker) has.
// Nothing is lost, repeated or reordered.

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
	// Each connection the channel has been on is a generation: 0 first, one more at each move.
	struct SharedState {
		std::mutex mutex;
		std::queue<QueuedPacket> packets;
		std::atomic<uint32_t> available_packets = 0;
		std::atomic<ChannelState> channel_state = STATE_CONNECTING;
		bool strict_order = false; // Ordered and reliable: a move keeps the order.
		uint32_t receiving = 0; // The generation messages are taken from.
		std::queue<QueuedPacket> held; // The next generation's, until the marker.
		std::string marker; // The other side's last message on the receiving generation, in a move.
		std::atomic<uint32_t> sending = 0; // The generation sent on, whose state is the channel's.
	};

	std::shared_ptr<SharedState> shared_state = std::make_shared<SharedState>();
	std::shared_ptr<rtc::DataChannel> channel; // Where packets are sent.
	std::shared_ptr<rtc::DataChannel> previous; // The old connection's, in a move, once sending moved.
	std::shared_ptr<rtc::DataChannel> next; // The new connection's, in a move, until sending moves.
	uint32_t generation = 0;
	QueuedPacket current_packet;
	WriteMode write_mode = WRITE_MODE_BINARY;
	bool negotiated = false;

	void bind(const std::shared_ptr<rtc::DataChannel> &p_channel, uint32_t p_generation);

protected:
	static void _bind_methods() {}

public:
	static Ref<WebRTCNativeDataChannel> wrap(const std::shared_ptr<rtc::DataChannel> &p_channel, bool p_negotiated);

	// Moving to another connection, for WebRTCNativePeerConnection. begin_move(): binds the new
	// connection's channel. expect_marker(): what the other side sends last on the old one.
	// send_moved(): sends p_marker on the old one (if this channel keeps order) and sends on the new
	// one from then on. has_received_move(): everything from the old one has arrived. finish_move()
	// lets go of the old one; cancel_move() of the new one.
	std::shared_ptr<rtc::DataChannel> get_channel() const { return channel; }
	void begin_move(const std::shared_ptr<rtc::DataChannel> &p_next);
	void expect_marker(const std::string &p_marker);
	void send_moved(const std::string &p_marker);
	bool has_received_move() const;
	void finish_move();
	void cancel_move();

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
