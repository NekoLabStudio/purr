#pragma once

// A frame's GPU time, for what scales or paces itself by it (DynamicResolution, FramePacing, the
// performance overlay): the viewport's timestamps, from its first to its last. On Metal that
// includes the GPU waiting between batches of work (measured against a Metal System Trace: 3.6 ms
// reported for 1.9 ms of work), and neither command buffers' times nor encoders' counters do better.

class Viewport;

namespace GpuTime {

// The last frame's, in milliseconds (0 until there's a measurement).
double frame_ms(Viewport *p_viewport);
// As the custom performance monitor graphics/gpu_ms (the editor's Monitors, and
// Performance.get_custom_monitor()), for the main viewport. Once the game's running.
void add_monitor();

} // namespace GpuTime
