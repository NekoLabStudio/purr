#pragma once

// Asking the player for the PIN a headset shows when it pairs with this Mac (macOS,
// pin_prompt.mm): a sheet on the game's window, answered with the digits or nothing (cancelled).
// The streamer asks for it with its `pin_needed` status line (streamer_process.cpp).

#ifdef __APPLE__

#include <functional>
#include <optional>
#include <string>

namespace pin_prompt {

// Shows the prompt, unless it's showing; `again` after a PIN that didn't match. `answer` runs on
// the main thread.
void ask(bool again, std::function<void(std::optional<std::string>)> answer);
// Takes the prompt away unanswered (the headset stopped pairing).
void dismiss();

}  // namespace pin_prompt

#endif
