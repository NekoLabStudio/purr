#pragma once

#include <functional>

// An Apple Vision Pro as the headset, on macOS 26 or later: the game's frames go to an immersive
// space macOS renders for the Vision Pro (vision_pro_scene.swift), in the game's own process.
// It plays the streamer's part on the shared frames (vr_shared.hpp): it reads the frames the
// runtime publishes and writes the head pose and optics back, so the rest of the runtime is the
// same for either headset. The head is tracked; the hands and controllers aren't (macOS has no
// hand tracking for a remote headset).
//
// Nothing shows unless a Vision Pro is nearby (the streamer watches the network for one): only
// then does it ask macOS for the space (macOS asks which Vision Pro, and the player accepts on it).
namespace vision_pro {

// What the runtime decides and does around the space. Called on a queue of their own.
struct Hooks {
    // Whether the space may open now (no other headset has the game).
    std::function<bool()> may_open;
    // The Vision Pro shows the game: other headsets step aside.
    std::function<void()> presenting;
    // The space closed: other headsets can have the game again.
    std::function<void()> ended;
};

// Shows the game in a Vision Pro, from when one is nearby, while VR runs.
void start(Hooks hooks);
// Whether a Vision Pro is on the network now (the streamer's vision_pro status). One appearing
// is asked for the space once, until it leaves again or VR restarts.
void nearby(bool present);
// Stops watching and closes the space.
void stop();

}  // namespace vision_pro
