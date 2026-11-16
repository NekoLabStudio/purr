#pragma once

// The Direct3D 11 backend (graphics_d3d11.cpp, Windows) and XR_KHR_D3D11_enable's call.

#ifdef _WIN32

#include "graphics.hpp"
#include "xr_platform.hpp"

std::unique_ptr<Graphics> make_d3d11_graphics(const XrGraphicsBindingD3D11KHR& binding);
XrResult d3d11_graphics_requirements(XrGraphicsRequirementsD3D11KHR* requirements);

#endif
