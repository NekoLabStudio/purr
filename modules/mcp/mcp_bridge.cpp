#include "mcp_bridge.h"

#include "core/config/project_settings.h"
#include "core/io/json.h"

namespace McpBridge {

String file_path() {
	return ProjectSettings::get_singleton()->globalize_path(ProjectSettings::get_singleton()->get_project_data_path()).path_join("purr_mcp.json");
}

bool Line::is_open() {
	peer->poll();
	const StreamPeerTCP::Status status = peer->get_status();
	return status == StreamPeerTCP::STATUS_CONNECTED || status == StreamPeerTCP::STATUS_CONNECTING;
}

bool Line::receive(Dictionary &r_message) {
	peer->poll();
	if (peer->get_status() == StreamPeerTCP::STATUS_CONNECTED) {
		uint8_t buffer[4096];
		int received = 0;
		while (peer->get_available_bytes() > 0 && peer->get_partial_data(buffer, sizeof(buffer), received) == OK && received > 0) {
			for (int i = 0; i < received; i++) {
				pending.push_back(buffer[i]);
			}
		}
	}
	for (uint32_t at = 0; at < pending.size(); at++) {
		if (pending[at] != '\n') {
			continue;
		}
		const String line = String::utf8((const char *)pending.ptr(), at);
		LocalVector<uint8_t> rest;
		for (uint32_t i = at + 1; i < pending.size(); i++) {
			rest.push_back(pending[i]);
		}
		pending = rest;
		const Variant parsed = JSON::parse_string(line);
		if (parsed.get_type() == Variant::DICTIONARY) {
			r_message = parsed;
			return true;
		}
		return false;
	}
	return false;
}

void Line::send(const Dictionary &p_message) {
	const CharString text = (JSON::stringify(p_message, "", false) + "\n").utf8();
	peer->put_data((const uint8_t *)text.get_data(), text.length());
}

} // namespace McpBridge
