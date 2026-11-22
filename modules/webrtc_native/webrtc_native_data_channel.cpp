#include "webrtc_native_data_channel.h"

#include <cstring>

Ref<WebRTCNativeDataChannel> WebRTCNativeDataChannel::wrap(const std::shared_ptr<rtc::DataChannel> &p_channel, bool p_negotiated) {
	ERR_FAIL_COND_V(!p_channel, Ref<WebRTCNativeDataChannel>());
	Ref<WebRTCNativeDataChannel> wrapper;
	wrapper.instantiate();
	wrapper->channel = p_channel;
	wrapper->negotiated = p_negotiated;
	const rtc::Reliability reliability = p_channel->reliability();
	wrapper->shared_state->strict_order = !reliability.unordered && !reliability.maxPacketLifeTime.has_value() && !reliability.maxRetransmits.has_value();
	wrapper->bind(p_channel, 0);
	return wrapper;
}

// Packets from a generation already left behind are dropped (there are none: the marker is the
// last), the next generation's wait for the marker, and only the sending generation's state counts.
void WebRTCNativeDataChannel::bind(const std::shared_ptr<rtc::DataChannel> &p_channel, uint32_t p_generation) {
	const std::weak_ptr<SharedState> weak_state = shared_state;
	auto queue_packet = [weak_state, p_generation](const uint8_t *p_data, size_t p_size, bool p_is_string) {
		const std::shared_ptr<SharedState> state = weak_state.lock();
		if (!state) {
			return;
		}
		std::lock_guard lock(state->mutex);
		QueuedPacket packet(std::vector<uint8_t>(p_data, p_data + p_size), p_is_string);
		if (!state->strict_order || p_generation == state->receiving) {
			if (state->strict_order && p_is_string && !state->marker.empty() && p_size == state->marker.size() && memcmp(p_data, state->marker.data(), p_size) == 0) {
				// The old connection's last: the new one's waiting messages come next.
				state->receiving++;
				state->marker.clear();
				while (!state->held.empty()) {
					state->packets.push(std::move(state->held.front()));
					state->held.pop();
				}
			} else {
				state->packets.push(std::move(packet));
			}
		} else if (p_generation == state->receiving + 1) {
			state->held.push(std::move(packet));
		}
		state->available_packets.store(state->packets.size(), std::memory_order_release);
	};
	auto set_state = [weak_state, p_generation](ChannelState p_state) {
		const std::shared_ptr<SharedState> state = weak_state.lock();
		if (state && state->sending.load(std::memory_order_acquire) == p_generation) {
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
}

void WebRTCNativeDataChannel::begin_move(const std::shared_ptr<rtc::DataChannel> &p_next) {
	next = p_next;
	bind(next, generation + 1);
}

void WebRTCNativeDataChannel::expect_marker(const std::string &p_marker) {
	std::lock_guard lock(shared_state->mutex);
	shared_state->marker = p_marker;
}

void WebRTCNativeDataChannel::send_moved(const std::string &p_marker) {
	ERR_FAIL_COND(!next);
	if (shared_state->strict_order && channel && channel->isOpen()) {
		try {
			channel->send(p_marker);
		} catch (...) {
		}
	}
	previous = channel;
	channel = next;
	next.reset();
	generation++;
	shared_state->sending.store(generation, std::memory_order_release);
	shared_state->channel_state.store(channel->isOpen() ? STATE_OPEN : STATE_CONNECTING, std::memory_order_release);
}

bool WebRTCNativeDataChannel::has_received_move() const {
	if (!shared_state->strict_order) {
		return true;
	}
	std::lock_guard lock(shared_state->mutex);
	return shared_state->receiving == generation;
}

void WebRTCNativeDataChannel::finish_move() {
	if (previous) {
		previous->resetCallbacks();
		previous.reset();
	}
	// The marker never came (the old connection ended first): what waited comes now.
	std::lock_guard lock(shared_state->mutex);
	if (shared_state->receiving < generation) {
		shared_state->receiving = generation;
		shared_state->marker.clear();
		while (!shared_state->held.empty()) {
			shared_state->packets.push(std::move(shared_state->held.front()));
			shared_state->held.pop();
		}
		shared_state->available_packets.store(shared_state->packets.size(), std::memory_order_release);
	}
}

void WebRTCNativeDataChannel::cancel_move() {
	if (next) {
		next->resetCallbacks();
		next.reset();
	}
	std::lock_guard lock(shared_state->mutex);
	shared_state->marker.clear();
	while (!shared_state->held.empty()) {
		shared_state->held.pop();
	}
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
