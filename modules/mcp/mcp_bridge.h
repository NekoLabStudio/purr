#pragma once

// The link between the MCP server and the Purr editor open on the same project: the editor
// listens on a local port and writes it, with a random token, to the project's
// .godot/purr_mcp.json; the server reads that and connects. Each side sends JSON a line:
// requests { token, id, method, params } and answers { id, result } or { id, error }.

#include "core/io/stream_peer_tcp.h"
#include "core/templates/local_vector.h"
#include "core/variant/dictionary.h"

namespace McpBridge {

// The connection file's path on disk.
String file_path();

// A connection that sends and receives whole lines of JSON.
class Line {
	Ref<StreamPeerTCP> peer;
	LocalVector<uint8_t> pending; // Received, not yet a whole line.

public:
	explicit Line(const Ref<StreamPeerTCP> &p_peer) :
			peer(p_peer) {}
	bool is_open();
	// The next whole message, if one has arrived.
	bool receive(Dictionary &r_message);
	void send(const Dictionary &p_message);
};

} // namespace McpBridge
