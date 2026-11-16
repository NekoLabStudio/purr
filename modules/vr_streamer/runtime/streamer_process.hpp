#pragma once

#include <functional>
#include <string>

// The streamer (streamer/, its own process beside the runtime library): it
// reads the frames the runtime publishes, encodes them and sends them to the
// headset through Steam Link, and sends the headset's pose back. It serves one
// headset connection and exits when it ends, so this keeps one running for
// as long as the session wants it.
namespace streamer_process {

// Starts the streamer for frames of this eye size and rate, published at
// `frames_path`. Does nothing if it's already running, or if another program
// starts it (PURR_VR_STREAMER=external).
void start(const std::string& frames_path, unsigned eye_width, unsigned eye_height, double hz);

// Stops it and waits for it to go.
void stop();

// Whether a headset is connected to the streamer now.
bool headset_connected();

// Told whether an Apple Vision Pro is on the network, as the streamer sees it (macOS).
void on_vision_pro(std::function<void(bool nearby)> listener);

}  // namespace streamer_process
