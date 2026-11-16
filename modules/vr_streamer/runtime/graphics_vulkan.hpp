#pragma once

// The Vulkan backend (graphics_vulkan.cpp) and XR_KHR_vulkan_enable2's calls.

#include "graphics.hpp"
#include "xr_platform.hpp"

std::unique_ptr<Graphics> make_vulkan_graphics(const XrGraphicsBindingVulkanKHR& binding);

XrResult vulkan_graphics_requirements(XrGraphicsRequirementsVulkanKHR* requirements);
XrResult vulkan_create_instance(const XrVulkanInstanceCreateInfoKHR* info, VkInstance* instance, VkResult* vk_result);
XrResult vulkan_graphics_device(const XrVulkanGraphicsDeviceGetInfoKHR* info, VkPhysicalDevice* device);
XrResult vulkan_create_device(const XrVulkanDeviceCreateInfoKHR* info, VkDevice* device, VkResult* vk_result);
