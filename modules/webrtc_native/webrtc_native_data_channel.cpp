#include "webrtc_native_data_channel.h"

#include <cstring>

Ref<WebRTCNativeDataChannel> WebRTCNativeDataChannel::wrap(const std::shared_ptr<rtc::DataChannel> &p_channel, bool p_negotiated) {
	ERR_FAIL_COND_V(!p_channel, Ref<WebRTCNativeDataChannel>());
	Ref<WebRTCNativeDataChannel> wrapper;
	wrapper.instantiate();
	wrapper->channel = p_channel;
	wrapper->negotiated = p_negotiated;

	const std::weak_ptr<SharedState> weak_state = wrapper->shared_state;
	auto queue_packet = [weak_state](const uint8_t *p_data, size_t p_size, bool p_is_string) {
		const std::shared_ptr<SharedState> state = weak_state.lock();
		if (!state) {
			return;
		}
		std::lock_guard lock(state->mutex);
		state->packets.emplace(std::vector<uint8_t>(p_data, p_data + p_size), p_is_string);
		state->available_packets.store(state->packets.size(), std::memory_order_release);
	};
	auto set_state = [weak_state](ChannelState p_state) {
		if (const std::shared_ptr<SharedState> state = weak_state.lock()) {
			state->channel_state.store(p_state, std::memory_order_release);
		}
	};

	p_channel->onMessage(
			[queue_packet](rtc::binary p_binary) {
				queue_packet(reinterpret_cast<const uint8_t *>(p_binary.data()), p_binary.size(), false);
			},
			[queue_packet](rtc::string p_string) {
				queue_packet(reinterpret_cast<const uint8_t *>(p_string.data()), p_string.size(), true);
			});
	p_channel->onOpen([set_state]() { set_state(STATE_OPEN); });
	p_channel->onClosed([set_state]() { set_state(STATE_CLOSED); });
	p_channel->onError([](std::string p_error) {
		ERR_PRINT(vformat("WebRTC data channel error: %s", String::utf8(p_error.c_str())));
	});
	return wrapper;
}

int WebRTCNativeDataChannel::get_available_packet_count() const {
	return shared_state->available_packets.load(std::memory_order_acquire);
}

Error WebRTCNativeDataChannel::get_packet(const uint8_t **r_buffer, int &r_buffer_size) {
	std::lock_guard lock(shared_state->mutex);
	ERR_FAIL_COND_V(shared_state->packets.empty(), ERR_UNAVAILABLE);
	current_packet = std::move(shared_state->packets.front());
	shared_state->packets.pop();
	shared_state->available_packets.store(shared_state->packets.size(), std::memory_order_release);
	// Valid until the next get_packet() or close().
	*r_buffer = current_packet.first.data();
	r_buffer_size = current_packet.first.size();
	return OK;
}

Error WebRTCNativeDataChannel::put_packet(const uint8_t *p_buffer, int p_buffer_size) {
	ERR_FAIL_COND_V(!channel || channel->isClosed(), FAILED);
	try {
		if (write_mode == WRITE_MODE_TEXT) {
			channel->send(std::string(reinterpret_cast<const char *>(p_buffer), strnlen(reinterpret_cast<const char *>(p_buffer), p_buffer_size)));
		} else {
			channel->send(reinterpret_cast<const std::byte *>(p_buffer), p_buffer_size);
		}
	} catch (const std::exception &e) {
		ERR_FAIL_V_MSG(FAILED, vformat("Couldn't send on the WebRTC data channel: %s", e.what()));
	}
	return OK;
}

int WebRTCNativeDataChannel::get_max_packet_size() const {
	return 16384; // RFC 8831, section 6.6.
}

void WebRTCNativeDataChannel::set_write_mode(WriteMode p_mode) {
	ERR_FAIL_COND(p_mode != WRITE_MODE_TEXT && p_mode != WRITE_MODE_BINARY);
	write_mode = p_mode;
}

WebRTCDataChannel::WriteMode WebRTCNativeDataChannel::get_write_mode() const {
	return write_mode;
}

bool WebRTCNativeDataChannel::was_string_packet() const {
	return current_packet.second;
}

WebRTCDataChannel::ChannelState WebRTCNativeDataChannel::get_ready_state() const {
	return shared_state->channel_state.load(std::memory_order_acquire);
}

String WebRTCNativeDataChannel::get_label() const {
	ERR_FAIL_COND_V(!channel, String());
	return String::utf8(channel->label().c_str());
}

bool WebRTCNativeDataChannel::is_ordered() const {
	ERR_FAIL_COND_V(!channel, false);
	return !channel->reliability().unordered;
}

int WebRTCNativeDataChannel::get_id() const {
	ERR_FAIL_COND_V(!channel, -1);
	return channel->id().value_or(-1);
}

int WebRTCNativeDataChannel::get_max_packet_life_time() const {
	ERR_FAIL_COND_V(!channel, -1);
	const auto &life_time = channel->reliability().maxPacketLifeTime;
	return life_time.has_value() ? int(life_time->count()) : -1;
}

int WebRTCNativeDataChannel::get_max_retransmits() const {
	ERR_FAIL_COND_V(!channel, -1);
	return channel->reliability().maxRetransmits.value_or(-1);
}

String WebRTCNativeDataChannel::get_protocol() const {
	ERR_FAIL_COND_V(!channel, String());
	return String::utf8(channel->protocol().c_str());
}

bool WebRTCNativeDataChannel::is_negotiated() const {
	return negotiated;
}

int WebRTCNativeDataChannel::get_buffered_amount() const {
	ERR_FAIL_COND_V(!channel, 0);
	return channel->bufferedAmount();
}

Error WebRTCNativeDataChannel::poll() {
	return OK; // Packets arrive on libdatachannel's threads.
}

void WebRTCNativeDataChannel::close() {
	if (!channel) {
		return;
	}
	try {
		channel->close();
	} catch (...) {
	}
}

WebRTCNativeDataChannel::~WebRTCNativeDataChannel() {
	close();
}
