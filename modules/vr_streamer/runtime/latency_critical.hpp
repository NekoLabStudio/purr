#pragma once

// While a VR session runs, the game tells macOS its work is latency-critical (latency_critical.mm),
// so it isn't throttled as a background app (App Nap) while the player looks at a headset rather
// than its window. Nothing on other systems (latency_critical_other.cpp).

namespace latency_critical {

void begin();
void end();

}  // namespace latency_critical
