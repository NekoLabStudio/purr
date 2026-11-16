#pragma once

// The graphics adapter, for the Direct3D backends (Windows): the one OpenXR's requirements calls
// name, and a device's adapter's name for the log.

#ifdef _WIN32

#include "xr_platform.hpp"

#include <string>

// The fastest adapter's LUID: it draws the game and encodes the video. False if there's none.
bool dxgi_high_performance_adapter(LUID& luid);
// The adapter's name, or empty.
std::string dxgi_adapter_name(const LUID& luid);

#endif
