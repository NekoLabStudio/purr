#pragma once

// The Direct3D 12 backend (graphics_d3d12.cpp, Windows) and XR_KHR_D3D12_enable's call.

#ifdef _WIN32

#include "graphics.hpp"
#include "xr_platform.hpp"

std::unique_ptr<Graphics> make_d3d12_graphics(const XrGraphicsBindingD3D12KHR& binding);
XrResult d3d12_graphics_requirements(XrGraphicsRequirementsD3D12KHR* requirements);

#endif
