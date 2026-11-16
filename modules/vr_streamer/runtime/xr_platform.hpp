#pragma once

// OpenXR's headers with every graphics binding this build offers: Metal on macOS, Vulkan everywhere
// and Direct3D 11 and 12 on Windows. OpenXR's platform header only declares a binding whose switch is set
// before it's first included, so everything includes it through here.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#endif

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN

#ifdef __APPLE__
#define XR_USE_GRAPHICS_API_METAL
#endif

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
