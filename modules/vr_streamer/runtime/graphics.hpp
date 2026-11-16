#pragma once

// The session's graphics: how swapchain images are made and how the eyes are copied out to the
// streamer. Metal on macOS (graphics_metal.mm), Vulkan everywhere (graphics_vulkan.cpp; on macOS
// through MoltenVK) and Direct3D 11 and 12 on Windows (graphics_d3d11.cpp, graphics_d3d12.cpp). Each copies the eyes in BGRA,
// which every encoder the streamer uses takes, without the game's thread waiting for the copy.

#include "shared_frames.hpp"
#include "xr_platform.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

constexpr std::uint32_t kSwapchainImageCount = 3;

class Graphics {
public:
    // One eye to copy: a swapchain image and its array layer.
    struct EyeSource {
        void* image = nullptr;
        std::uint32_t array_index = 0;
    };

    virtual ~Graphics() = default;

    virtual std::string device_name() const = 0;
    // The swapchain formats offered, colour first (BGRA, sRGB-encoded).
    virtual const std::vector<std::int64_t>& formats() const = 0;
    virtual bool is_depth_format(std::int64_t format) const = 0;
    // The shared file's pixel format for a colour format (unknown for others).
    virtual vr_shared::PixelFormat shared_format(std::int64_t format) const = 0;

    // Makes a swapchain's images, in the layout OpenXR hands them to the game in.
    virtual bool create_images(const XrSwapchainCreateInfo& info, void* images[kSwapchainImageCount]) = 0;
    virtual void destroy_images(void* images[kSwapchainImageCount]) = 0;
    // Fills the game's array of XrSwapchainImage*KHR, checking its type.
    virtual XrResult enumerate_images(void* const images[kSwapchainImageCount], XrSwapchainImageBaseHeader* out) const = 0;
    // Readies an image the first time the game acquires it (Vulkan: its initial layout).
    virtual void prepare_image(void*) {}

    // Whether the copy must wait for the next xrWaitFrame: Metal's driver commits a frame's work
    // after xrEndFrame returns. Vulkan and Direct3D have submitted it by xrEndFrame.
    virtual bool copies_after_end_frame() const { return false; }
    // Prepares to copy into the shared file, once it exists.
    virtual bool start_export(SharedFrames& frames) = 0;
    // Copies both eyes into a slot and publishes it when the copy lands.
    virtual void export_frame(SharedFrames& frames, const EyeSource eyes[2], const SharedFrames::Frame& frame) = 0;
    // Waits for copies in flight and lets go of the shared file.
    virtual void stop_export(SharedFrames& frames) = 0;
};

#ifdef __APPLE__
std::unique_ptr<Graphics> make_metal_graphics(void* command_queue);
// The Metal device for xrGetMetalGraphicsRequirementsKHR.
void* metal_default_device();
#endif
