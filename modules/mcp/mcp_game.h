#pragma once

// A game run from the Purr editor answers the MCP server through the editor's debugger: its live
// scene tree, its log, its performance monitors, a command in Purr's console, an expression, a
// screenshot. Messages "purr_mcp:<request>" with [key, params] come in; "purr_mcp:reply" with
// [key, result] goes back, result holding "text" or "error" and whatever data there is.

namespace McpGame {

// In a game started by the editor's debugger: answer the editor from the first frame.
void start_if_debugged();
void stop();

} // namespace McpGame
