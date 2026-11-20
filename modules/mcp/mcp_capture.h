#pragma once

// In a game the MCP server started for a screenshot or a measurement (with --purr-mcp-capture after
// --): the game runs as usual, its window kept in front (a window behind others is neither drawn
// nor timed truly on macOS), and after a while this saves what's on screen, or the frame times, to
// a file and quits.

#include "core/templates/local_vector.h"
#include "scene/main/node.h"

class McpCapture : public Node {
	GDCLASS(McpCapture, Node);

	String mode; // "screenshot" or "measure"
	String out_path;
	double seconds = 2.0;
	double elapsed = 0.0;
	uint64_t last_usec = 0;
	LocalVector<double> frames_ms;
	double gpu_sum = 0.0;
	int gpu_frames = 0;

	void _finish();

protected:
	void _notification(int p_what);

public:
	// Joins the tree on the first frame if the game was started for it.
	static void start_if_asked();

	McpCapture();
};
