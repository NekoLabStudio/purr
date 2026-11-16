#include "webrtc_native_peer_connection.h"

#include "webrtc_native_data_channel.h"

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

Error WebRTCNativePeerConnection::initialize(const Dictionary &p_config) {
	close();
	rtc::Configuration config;
	if (p_config.get("iceServers", Variant()).get_type() == Variant::ARRAY) {
		const Array servers = p_config["iceServers"];
		for (const Variant &server : servers) {
			ERR_FAIL_COND_V_MSG(server.get_type() != Variant::DICTIONARY, ERR_INVALID_PARAMETER, "Each of \"iceServers\" must be a dictionary.");
			ERR_FAIL_COND_V(parse_ice_server(config, server) != OK, ERR_INVALID_PARAMETER);
		}
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
	return create_peer_connection(config);
}

Error WebRTCNativePeerConnection::create_peer_connection(rtc::Configuration &r_config) {
	// Offers are made when the game asks for one, as in browsers.
	r_config.disableAutoNegotiation = true;
	try {
		peer_connection = std::make_shared<rtc::PeerConnection>(r_config);
	} catch (const std::exception &e) {
		ERR_FAIL_V_MSG(FAILED, vformat("Couldn't create the WebRTC peer connection: %s", e.what()));
	}

	const std::weak_ptr<SharedState> weak_state = shared_state;
	auto queue_task = [weak_state](std::function<void(Object *)> p_task) {
		if (const std::shared_ptr<SharedState> state = weak_state.lock()) {
			std::lock_guard lock(state->mutex);
			state->tasks.push(std::move(p_task));
		}
	};
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
			p_object->emit_signal(SNAME("data_channel_received"), channel);
		});
	});
	return OK;
}

Ref<WebRTCDataChannel> WebRTCNativePeerConnection::create_data_channel(const String &p_label, const Dictionary &p_options) {
	ERR_FAIL_COND_V(!peer_connection, Ref<WebRTCDataChannel>());
	rtc::DataChannelInit init;
	ERR_FAIL_COND_V(parse_channel_options(init, p_options) != OK, Ref<WebRTCDataChannel>());
	try {
		const std::shared_ptr<rtc::DataChannel> channel = peer_connection->createDataChannel(p_label.utf8().get_data(), init);
		ERR_FAIL_COND_V(!channel, Ref<WebRTCDataChannel>());
		return WebRTCNativeDataChannel::wrap(channel, init.negotiated);
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
	return OK;
}

void WebRTCNativePeerConnection::close() {
	if (peer_connection) {
		try {
			peer_connection->close();
		} catch (...) {
		}
	}
	// Anything still queued belongs to the closed connection.
	shared_state = std::make_shared<SharedState>();
}

WebRTCNativePeerConnection::WebRTCNativePeerConnection() {
	initialize(Dictionary());
}

WebRTCNativePeerConnection::~WebRTCNativePeerConnection() {
	close();
}
