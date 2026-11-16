#pragma once

// The runtime's log, started afresh each time the game starts VR: the file
// PURR_VR_LOG names (Purr sets it to the game's user://logs/vr.log).
// Lifecycle and failures only, so it stays short.
void runtime_log(const char* message);
void runtime_logf(const char* format, ...) __attribute__((format(printf, 1, 2)));

// The folder holding the runtime library (and the streamer beside it), with
// a trailing slash.
const char* runtime_directory();

// Where the runtime and streamer keep what lasts between runs (the headset's
// optics, the streamer's log, the player's choices): the folder PURR_VR_DATA
// names (Purr sets it to the game's user://vr/), created on first use, with
// a trailing slash.
const char* runtime_data_directory();
