#include "webrtc_native_peer_connection.h"

#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/os/os.h"

namespace {

void log_message(rtc::LogLevel p_level, std::string p_message) {
	const String message = String::utf8(p_message.c_str());
	switch (p_level) {
		case rtc::LogLevel::Fatal:
		case rtc::LogLevel::Error:
			ERR_PRINT(message);
			break;
		case rtc::LogLevel::Warning:
			WARN_PRINT(message);
			break;
		default:
			print_verbose(message);
			break;
	}
}

Error parse_ice_server(rtc::Configuration &r_config, const Dictionary &p_server) {
	ERR_FAIL_COND_V_MSG(!p_server.has("urls"), ERR_INVALID_PARAMETER, "An ICE server needs \"urls\".");
	Array urls;
	const Variant urls_value = p_server["urls"];
	if (urls_value.get_type() == Variant::STRING) {
		urls.push_back(urls_value);
	} else if (urls_value.get_type() == Variant::ARRAY) {
		urls = urls_value;
	} else {
		ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "An ICE server's \"urls\" must be a string or an array of strings.");
	}
	// Credentials only mean something for TURN, which takes a password.
	const String username = p_server.get("username", String());
	const String credential = p_server.get("credential", String());
	for (const Variant &url : urls) {
		rtc::IceServer server(String(url).utf8().get_data());
		server.username = username.utf8().get_data();
		server.password = credential.utf8().get_data();
		r_config.iceServers.push_back(server);
	}
	return OK;
}

Error parse_channel_options(rtc::DataChannelInit &r_init, const Dictionary &p_options) {
	if (p_options.has("negotiated")) {
		r_init.negotiated = p_options["negotiated"];
	}
	if (p_options.has("id")) {
		r_init.id = uint16_t(int(p_options["id"]));
	}
	ERR_FAIL_COND_V_MSG(r_init.negotiated != r_init.id.has_value(), ERR_INVALID_PARAMETER, "A negotiated data channel needs an \"id\", and only a negotiated one can have it.");
	ERR_FAIL_COND_V_MSG(p_options.has("maxPacketLifeTime") && p_options.has("maxRetransmits"), ERR_INVALID_PARAMETER, "A data channel can limit \"maxPacketLifeTime\" or \"maxRetransmits\", not both.");
	if (p_options.has("maxPacketLifeTime")) {
		r_init.reliability.maxPacketLifeTime = std::chrono::milliseconds(int(p_options["maxPacketLifeTime"]));
	} else if (p_options.has("maxRetransmits")) {
		r_init.reliability.maxRetransmits = int(p_options["maxRetransmits"]);
	}
	if (p_options.has("ordered") && !bool(p_options["ordered"])) {
		r_init.reliability.unordered = true;
	}
	if (p_options.has("protocol")) {
		r_init.protocol = String(p_options["protocol"]).utf8().get_data();
	}
	return OK;
}

} // namespace

void WebRTCNativePeerConnection::initialize_logging() {
#ifdef DEBUG_ENABLED
	rtc::InitLogger(rtc::LogLevel::Info, log_message);
#else
	rtc::InitLogger(rtc::LogLevel::Warning, log_message);
#endif
}

void WebRTCNativePeerConnection::cleanup() {
	rtc::Cleanup().wait();
}

WebRTCPeerConnection::ConnectionState WebRTCNativePeerConnection::get_connection_state() const {
	ERR_FAIL_COND_V(!peer_connection, STATE_CLOSED);
	switch (peer_connection->state()) {
		case rtc::PeerConnection::State::New:
			return STATE_NEW;
		case rtc::PeerConnection::State::Connecting:
			return STATE_CONNECTING;
		case rtc::PeerConnection::State::Connected:
			return STATE_CONNECTED;
		case rtc::PeerConnection::State::Disconnected:
			return STATE_DISCONNECTED;
		case rtc::PeerConnection::State::Failed:
			return STATE_FAILED;
		default:
			return STATE_CLOSED;
	}
}

WebRTCPeerConnection::GatheringState WebRTCNativePeerConnection::get_gathering_state() const {
	ERR_FAIL_COND_V(!peer_connection, GATHERING_STATE_NEW);
	switch (peer_connection->gatheringState()) {
		case rtc::PeerConnection::GatheringState::InProgress:
			return GATHERING_STATE_GATHERING;
		case rtc::PeerConnection::GatheringState::Complete:
			return GATHERING_STATE_COMPLETE;
		default:
			return GATHERING_STATE_NEW;
	}
}

WebRTCPeerConnection::SignalingState WebRTCNativePeerConnection::get_signaling_state() const {
	ERR_FAIL_COND_V(!peer_connection, SIGNALING_STATE_CLOSED);
	switch (peer_connection->signalingState()) {
		case rtc::PeerConnection::SignalingState::Stable:
			return SIGNALING_STATE_STABLE;
		case rtc::PeerConnection::SignalingState::HaveLocalOffer:
			return SIGNALING_STATE_HAVE_LOCAL_OFFER;
		case rtc::PeerConnection::SignalingState::HaveRemoteOffer:
			return SIGNALING_STATE_HAVE_REMOTE_OFFER;
		case rtc::PeerConnection::SignalingState::HaveLocalPranswer:
			return SIGNALING_STATE_HAVE_LOCAL_PRANSWER;
		case rtc::PeerConnection::SignalingState::HaveRemotePranswer:
			return SIGNALING_STATE_HAVE_REMOTE_PRANSWER;
		default:
			return SIGNALING_STATE_CLOSED;
	}
}

namespace {

Error parse_ice_servers(rtc::Configuration &r_config, const Array &p_servers) {
	r_config.iceServers.clear();
	for (const Variant &server : p_servers) {
		ERR_FAIL_COND_V_MSG(server.get_type() != Variant::DICTIONARY, ERR_INVALID_PARAMETER, "Each of \"iceServers\" must be a dictionary.");
		ERR_FAIL_COND_V(parse_ice_server(r_config, server) != OK, ERR_INVALID_PARAMETER);
	}
	return OK;
}

std::string random_hex(int p_bytes) {
	uint8_t bytes[32];
	if (OS::get_singleton()->get_entropy(bytes, p_bytes) != OK) {
		for (int i = 0; i < p_bytes; i++) {
			bytes[i] = uint8_t(Math::rand());
		}
	}
	return String::hex_encode_buffer(bytes, p_bytes).utf8().get_data();
}

} // namespace

Error WebRTCNativePeerConnection::initialize(const Dictionary &p_config) {
	close();
	rtc::Configuration config;
	if (p_config.get("iceServers", Variant()).get_type() == Variant::ARRAY) {
		ERR_FAIL_COND_V(parse_ice_servers(config, p_config["iceServers"]) != OK, ERR_INVALID_PARAMETER);
	}
	// "relay": connect only through TURN, so the other side never sees this device's address.
	if (p_config.get("iceTransportPolicy", "all") == Variant("relay")) {
		config.iceTransportPolicy = rtc::TransportPolicy::Relay;
	}
	// libdatachannel's own settings, under the same key webrtc-native reads.
	if (p_config.get("libdatachannel", Variant()).get_type() == Variant::DICTIONARY) {
		const Dictionary options = p_config["libdatachannel"];
		if (options.has("enableIceTcp")) {
			config.enableIceTcp = options["enableIceTcp"];
		}
		if (options.has("enableIceUdpMux")) {
			config.enableIceUdpMux = options["enableIceUdpMux"];
		}
		if (options.has("portRangeBegin")) {
			config.portRangeBegin = int(options["portRangeBegin"]);
		}
		if (options.has("portRangeEnd")) {
			config.portRangeEnd = int(options["portRangeEnd"]);
		}
		if (options.has("mtu")) {
			config.mtu = int(options["mtu"]);
		}
	}
	// Offers are made when the game asks for one, as in browsers.
	config.disableAutoNegotiation = true;
	configuration = config;
	return create_peer_connection(config);
}

// Queues work for the main thread, which poll() does.
std::function<void(std::function<void(Object *)>)> WebRTCNativePeerConnection::task_queue() {
	const std::weak_ptr<SharedState> weak_state = shared_state;
	return [weak_state](std::function<void(Object *)> p_task) {
		if (const std::shared_ptr<SharedState> state = weak_state.lock()) {
			std::lock_guard lock(state->mutex);
			state->tasks.push(std::move(p_task));
		}
	};
}

Error WebRTCNativePeerConnection::create_peer_connection(rtc::Configuration &r_config) {
	try {
		peer_connection = std::make_shared<rtc::PeerConnection>(r_config);
	} catch (const std::exception &e) {
		ERR_FAIL_V_MSG(FAILED, vformat("Couldn't create the WebRTC peer connection: %s", e.what()));
	}

	auto queue_task = task_queue();
	peer_connection->onLocalDescription([queue_task](rtc::Description p_description) {
		const String type = p_description.type() == rtc::Description::Type::Offer ? "offer" : "answer";
		const String sdp = String::utf8(std::string(p_description).c_str());
		queue_task([type, sdp](Object *p_object) {
			p_object->emit_signal(SNAME("session_description_created"), type, sdp);
		});
	});
	peer_connection->onLocalCandidate([queue_task](rtc::Candidate p_candidate) {
		const String mid = String::utf8(p_candidate.mid().c_str());
		const String candidate = String::utf8(p_candidate.candidate().c_str());
		queue_task([mid, candidate](Object *p_object) {
			p_object->emit_signal(SNAME("ice_candidate_created"), mid, 0, candidate);
		});
	});
	peer_connection->onDataChannel([queue_task](std::shared_ptr<rtc::DataChannel> p_channel) {
		const Ref<WebRTCNativeDataChannel> channel = WebRTCNativeDataChannel::wrap(p_channel, false);
		queue_task([channel](Object *p_object) {
			static_cast<WebRTCNativePeerConnection *>(p_object)->record(channel);
			p_object->emit_signal(SNAME("data_channel_received"), channel);
		});
	});
	open_control(peer_connection, control);
	return OK;
}

// The channel moves are negotiated on. Negotiated, so it's there on both sides without an offer.
void WebRTCNativePeerConnection::open_control(const std::shared_ptr<rtc::PeerConnection> &p_connection, std::shared_ptr<rtc::DataChannel> &r_control) {
	rtc::DataChannelInit init;
	init.negotiated = true;
	init.id = CONTROL_CHANNEL_ID;
	try {
		r_control = p_connection->createDataChannel("purr-move", init);
	} catch (const std::exception &e) {
		ERR_PRINT(vformat("Couldn't create the WebRTC control channel: %s", e.what()));
		return;
	}
	auto queue_task = task_queue();
	r_control->onMessage([](rtc::binary) {}, [queue_task](rtc::string p_text) {
		const String text = String::utf8(p_text.c_str());
		queue_task([text](Object *p_object) {
			static_cast<WebRTCNativePeerConnection *>(p_object)->control_received(text);
		});
	});
}

void WebRTCNativePeerConnection::record(const Ref<WebRTCNativeDataChannel> &p_wrapper) {
	const std::shared_ptr<rtc::DataChannel> channel = p_wrapper->get_channel();
	if (!channel) {
		return;
	}
	// On the connection moved to, every channel is negotiated, with the ID it has by then (one the
	// game didn't negotiate only gets its ID once the connection is up).
	ChannelRecord record;
	record.wrapper = p_wrapper;
	record.label = channel->label();
	record.init.reliability = channel->reliability();
	record.init.protocol = channel->protocol();
	record.init.negotiated = true;
	records.push_back(record);
}

Ref<WebRTCDataChannel> WebRTCNativePeerConnection::create_data_channel(const String &p_label, const Dictionary &p_options) {
	ERR_FAIL_COND_V(!peer_connection, Ref<WebRTCDataChannel>());
	rtc::DataChannelInit init;
	ERR_FAIL_COND_V(parse_channel_options(init, p_options) != OK, Ref<WebRTCDataChannel>());
	try {
		const std::shared_ptr<rtc::DataChannel> channel = peer_connection->createDataChannel(p_label.utf8().get_data(), init);
		ERR_FAIL_COND_V(!channel, Ref<WebRTCDataChannel>());
		const Ref<WebRTCNativeDataChannel> wrapper = WebRTCNativeDataChannel::wrap(channel, init.negotiated);
		record(wrapper);
		return wrapper;
	} catch (const std::exception &e) {
		ERR_FAIL_V_MSG(Ref<WebRTCDataChannel>(), vformat("Couldn't create the WebRTC data channel: %s", e.what()));
	}
}

Error WebRTCNativePeerConnection::create_offer() {
	ERR_FAIL_COND_V(!peer_connection, ERR_UNCONFIGURED);
	ERR_FAIL_COND_V(get_connection_state() != STATE_NEW, FAILED);
	try {
		peer_connection->setLocalDescription(rtc::Description::Type::Offer);
	} catch (const std::exception &e) {
		ERR_FAIL_V_MSG(FAILED, vformat("Couldn't create the WebRTC offer: %s", e.what()));
	}
	return OK;
}

Error WebRTCNativePeerConnection::set_remote_description(const String &p_type, const String &p_sdp) {
	ERR_FAIL_COND_V(!peer_connection, ERR_UNCONFIGURED);
	try {
		peer_connection->setRemoteDescription(rtc::Description(p_sdp.utf8().get_data(), p_type.utf8().get_data()));
		// The answer, as browsers make it.
		if (p_type == "offer") {
			peer_connection->setLocalDescription(rtc::Description::Type::Answer);
		}
	} catch (const std::exception &e) {
		ERR_FAIL_V_MSG(FAILED, vformat("Couldn't use the remote WebRTC description: %s", e.what()));
	}
	return OK;
}

Error WebRTCNativePeerConnection::set_local_description(const String &p_type, const String &p_sdp) {
	ERR_FAIL_COND_V(!peer_connection, ERR_UNCONFIGURED);
	// libdatachannel sets the local description itself when it creates an offer or answer.
	return OK;
}

Error WebRTCNativePeerConnection::add_ice_candidate(const String &p_sdp_mid_name, int p_sdp_mline_index_name, const String &p_sdp_name) {
	ERR_FAIL_COND_V(!peer_connection, ERR_UNCONFIGURED);
	try {
		peer_connection->addRemoteCandidate(rtc::Candidate(p_sdp_name.utf8().get_data(), p_sdp_mid_name.utf8().get_data()));
	} catch (const std::exception &e) {
		ERR_FAIL_V_MSG(FAILED, vformat("Couldn't add the WebRTC ICE candidate: %s", e.what()));
	}
	return OK;
}

Error WebRTCNativePeerConnection::poll() {
	ERR_FAIL_COND_V(!peer_connection, ERR_UNCONFIGURED);
	std::queue<std::function<void(Object *)>> tasks;
	{
		std::lock_guard lock(shared_state->mutex);
		tasks.swap(shared_state->tasks);
	}
	while (!tasks.empty()) {
		tasks.front()(this);
		tasks.pop();
	}
	advance_move();
	return OK;
}

void WebRTCNativePeerConnection::close() {
	if (move) {
		cancel_move();
	}
	if (peer_connection) {
		try {
			peer_connection->close();
		} catch (...) {
		}
	}
	records.clear();
	control.reset();
	move_waiting = false;
	// Anything still queued belongs to the closed connection.
	shared_state = std::make_shared<SharedState>();
}

// Moving to new ICE servers.

// Whether this side reaches the other through a TURN server: then its credentials matter.
bool WebRTCNativePeerConnection::uses_relay() {
	rtc::Candidate local;
	rtc::Candidate remote;
	return peer_connection && peer_connection->getSelectedCandidatePair(&local, &remote) && local.type() == rtc::Candidate::Type::Relayed;
}

Error WebRTCNativePeerConnection::renew_ice_servers(const Array &p_ice_servers) {
	ERR_FAIL_COND_V(!peer_connection, ERR_UNCONFIGURED);
	rtc::Configuration config = configuration;
	ERR_FAIL_COND_V(parse_ice_servers(config, p_ice_servers) != OK, ERR_INVALID_PARAMETER);
	configuration = config;
	if (move) {
		move_waiting = true;
		return OK;
	}
	ERR_FAIL_COND_V_MSG(get_connection_state() != STATE_CONNECTED, ERR_UNAVAILABLE, "Only a connected WebRTC connection can move to new ICE servers.");
	return start_move(true, std::string());
}

Error WebRTCNativePeerConnection::start_move(bool p_initiator, const std::string &p_remote_marker) {
	std::unique_ptr<Move> started = std::make_unique<Move>();
	started->initiator = p_initiator;
	started->marker = "purr-move:" + random_hex(16);
	started->remote_marker = p_remote_marker;
	started->nonce = uint64_t(std::stoull(random_hex(7), nullptr, 16));
	started->started_msec = OS::get_singleton()->get_ticks_msec();
	try {
		started->connection = std::make_shared<rtc::PeerConnection>(configuration);
	} catch (const std::exception &e) {
		ERR_FAIL_V_MSG(FAILED, vformat("Couldn't create the WebRTC connection to move to: %s", e.what()));
	}
	// Its offer, answer and candidates go over the control channel, never to the game; a task left
	// over from a move that ended does nothing.
	auto queue_task = task_queue();
	const uint64_t nonce = started->nonce;
	started->connection->onLocalDescription([queue_task, nonce](rtc::Description p_description) {
		const String type = p_description.type() == rtc::Description::Type::Offer ? "offer" : "answer";
		const String sdp = String::utf8(std::string(p_description).c_str());
		queue_task([type, sdp, nonce](Object *p_object) {
			WebRTCNativePeerConnection *self = static_cast<WebRTCNativePeerConnection *>(p_object);
			if (self->move && self->move->nonce == nonce) {
				Dictionary message;
				message["t"] = type;
				message["sdp"] = sdp;
				message["marker"] = String::utf8(self->move->marker.c_str());
				message["nonce"] = String::num_uint64(nonce);
				self->send_control(message);
			}
		});
	});
	started->connection->onLocalCandidate([queue_task, nonce](rtc::Candidate p_candidate) {
		const String mid = String::utf8(p_candidate.mid().c_str());
		const String candidate = String::utf8(p_candidate.candidate().c_str());
		queue_task([mid, candidate, nonce](Object *p_object) {
			WebRTCNativePeerConnection *self = static_cast<WebRTCNativePeerConnection *>(p_object);
			if (self->move && self->move->nonce == nonce) {
				Dictionary message;
				message["t"] = "candidate";
				message["candidate"] = candidate;
				message["mid"] = mid;
				message["nonce"] = String::num_uint64(nonce);
				self->send_control(message);
			}
		});
	});
	open_control(started->connection, started->control);
	for (ChannelRecord &record : records) {
		std::shared_ptr<rtc::DataChannel> channel;
		rtc::DataChannelInit init = record.init;
		init.id = record.wrapper->get_channel()->id();
		if (!init.id.has_value()) {
			started->connection->close();
			ERR_FAIL_V_MSG(ERR_UNAVAILABLE, "A data channel has no ID yet: the WebRTC connection can only move once it's connected.");
		}
		try {
			channel = started->connection->createDataChannel(record.label, init);
		} catch (const std::exception &e) {
			started->connection->close();
			ERR_FAIL_V_MSG(FAILED, vformat("Couldn't create a data channel on the WebRTC connection to move to: %s", e.what()));
		}
		started->channels.push_back(channel);
	}
	move = std::move(started);
	for (size_t i = 0; i < records.size(); i++) {
		records[i].wrapper->begin_move(move->channels[i]);
		if (!p_remote_marker.empty()) {
			records[i].wrapper->expect_marker(p_remote_marker);
		}
	}
	if (p_initiator) {
		try {
			move->connection->setLocalDescription(rtc::Description::Type::Offer);
		} catch (const std::exception &e) {
			cancel_move();
			ERR_FAIL_V_MSG(FAILED, vformat("Couldn't offer the WebRTC connection to move to: %s", e.what()));
		}
	}
	return OK;
}

void WebRTCNativePeerConnection::send_control(const Dictionary &p_message) {
	if (!control || !control->isOpen()) {
		return;
	}
	try {
		control->send(std::string(JSON::stringify(p_message).utf8().get_data()));
	} catch (...) {
	}
}

void WebRTCNativePeerConnection::control_received(const String &p_text) {
	const Variant parsed = JSON::parse_string(p_text);
	if (parsed.get_type() != Variant::DICTIONARY) {
		return;
	}
	const Dictionary message = parsed;
	const String type = message.get("t", "");
	const uint64_t nonce = String(message.get("nonce", "0")).to_int();
	// A candidate or "moved" from a move that has ended (one that lost to a move started at the
	// same moment) is ignored.
	if ((type == "candidate" || type == "moved") && (!move || move->remote_nonce != nonce)) {
		return;
	}
	try {
		if (type == "offer") {
			// Both sides started at once: the higher nonce's move goes ahead.
			if (move && move->initiator && move->nonce > nonce) {
				return;
			}
			if (move) {
				cancel_move();
			}
			if (start_move(false, String(message.get("marker", "")).utf8().get_data()) != OK) {
				return;
			}
			move->remote_nonce = nonce;
			move->connection->setRemoteDescription(rtc::Description(String(message.get("sdp", "")).utf8().get_data(), "offer"));
			move->connection->setLocalDescription(rtc::Description::Type::Answer);
		} else if (type == "answer" && move && move->initiator) {
			move->remote_nonce = nonce;
			move->remote_marker = String(message.get("marker", "")).utf8().get_data();
			for (ChannelRecord &record : records) {
				record.wrapper->expect_marker(move->remote_marker);
			}
			move->connection->setRemoteDescription(rtc::Description(String(message.get("sdp", "")).utf8().get_data(), "answer"));
		} else if (type == "candidate") {
			try {
				move->connection->addRemoteCandidate(rtc::Candidate(String(message.get("candidate", "")).utf8().get_data(), String(message.get("mid", "")).utf8().get_data()));
			} catch (const std::exception &e) {
				// One candidate that can't be used doesn't stop the move.
				print_verbose(vformat("Skipped a WebRTC candidate while moving: %s", e.what()));
			}
		} else if (type == "moved") {
			move->remote_moved = true;
		}
	} catch (const std::exception &e) {
		ERR_PRINT(vformat("Couldn't move the WebRTC connection: %s", e.what()));
		if (move) {
			cancel_move();
		}
	}
}

// Each poll: once the new connection and its channels are open, this side sends on them; once
// both sides do and everything sent on the old one has arrived, the old one closes.
void WebRTCNativePeerConnection::advance_move() {
	if (!move) {
		return;
	}
	const uint64_t time = OS::get_singleton()->get_ticks_msec();
	if (!move->moved) {
		bool open = move->connection->state() == rtc::PeerConnection::State::Connected && move->control && move->control->isOpen();
		for (const std::shared_ptr<rtc::DataChannel> &channel : move->channels) {
			open = open && channel->isOpen();
		}
		if (!open) {
			if (time - move->started_msec > MOVE_TIMEOUT_MSEC) {
				WARN_PRINT("The WebRTC connection couldn't move to its new ICE servers; it carries on with its old ones.");
				cancel_move();
			}
			return;
		}
		for (ChannelRecord &record : records) {
			record.wrapper->send_moved(move->marker);
		}
		Dictionary message;
		message["t"] = "moved";
		message["nonce"] = String::num_uint64(move->nonce);
		send_control(message);
		move->moved = true;
		move->settled_msec = 0;
	}
	bool received = move->remote_moved;
	for (const ChannelRecord &record : records) {
		received = received && record.wrapper->has_received_move();
	}
	if (received && move->settled_msec == 0) {
		move->settled_msec = time;
	}
	// A moment for the last of the old connection's unordered packets, or, if the other side never
	// says it moved, a while longer before giving up on it.
	if ((move->settled_msec != 0 && time - move->settled_msec > MOVE_SETTLE_MSEC) || time - move->started_msec > 2 * MOVE_TIMEOUT_MSEC) {
		complete_move();
	}
}

void WebRTCNativePeerConnection::complete_move() {
	const std::shared_ptr<rtc::PeerConnection> old = peer_connection;
	const std::shared_ptr<rtc::DataChannel> old_control = control;
	for (ChannelRecord &record : records) {
		record.wrapper->finish_move();
	}
	peer_connection = move->connection;
	control = move->control;
	move.reset();
	if (old_control) {
		old_control->resetCallbacks();
	}
	try {
		old->close();
	} catch (...) {
	}
	if (move_waiting) {
		move_waiting = false;
		start_move(true, std::string());
	}
}

void WebRTCNativePeerConnection::cancel_move() {
	for (ChannelRecord &record : records) {
		record.wrapper->cancel_move();
	}
	if (move->control) {
		move->control->resetCallbacks();
	}
	try {
		move->connection->close();
	} catch (...) {
	}
	move.reset();
}

void WebRTCNativePeerConnection::_bind_methods() {
	ClassDB::bind_method(D_METHOD("renew_ice_servers", "ice_servers"), &WebRTCNativePeerConnection::renew_ice_servers);
	ClassDB::bind_method(D_METHOD("uses_relay"), &WebRTCNativePeerConnection::uses_relay);
	ClassDB::bind_method(D_METHOD("is_renewing"), &WebRTCNativePeerConnection::is_renewing);
}

WebRTCNativePeerConnection::WebRTCNativePeerConnection() {
	initialize(Dictionary());
}

WebRTCNativePeerConnection::~WebRTCNativePeerConnection() {
	close();
}
