#include "graphics.hpp"

#include "runtime_log.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>


namespace {

class MetalGraphics final : public Graphics {
public:
    explicit MetalGraphics(id<MTLCommandQueue> queue) : queue_(queue), device_(queue.device) {}

    std::string device_name() const override { return device_.name.UTF8String; }

    const std::vector<std::int64_t>& formats() const override {
        static const std::vector<std::int64_t> kFormats = {MTLPixelFormatBGRA8Unorm_sRGB, MTLPixelFormatDepth32Float,
                                                           MTLPixelFormatDepth32Float_Stencil8};
        return kFormats;
    }

    bool is_depth_format(std::int64_t format) const override {
        return format == MTLPixelFormatDepth32Float || format == MTLPixelFormatDepth32Float_Stencil8;
    }

    vr_shared::PixelFormat shared_format(std::int64_t format) const override {
        return format == MTLPixelFormatBGRA8Unorm_sRGB || format == MTLPixelFormatBGRA8Unorm ? vr_shared::PixelFormat::bgra8
                                                                                           : vr_shared::PixelFormat::unknown;
    }

    bool create_images(const XrSwapchainCreateInfo& info, void* images[kSwapchainImageCount]) override {
        MTLTextureDescriptor* descriptor = [[MTLTextureDescriptor alloc] init];
        const bool array = info.arraySize > 1;
        const bool multisampled = info.sampleCount > 1;
        descriptor.textureType = multisampled ? (array ? MTLTextureType2DMultisampleArray : MTLTextureType2DMultisample)
                                              : (array ? MTLTextureType2DArray : MTLTextureType2D);
        descriptor.pixelFormat = static_cast<MTLPixelFormat>(info.format);
        descriptor.width = info.width;
        descriptor.height = info.height;
        descriptor.arrayLength = info.arraySize;
        descriptor.mipmapLevelCount = info.mipCount;
        descriptor.sampleCount = info.sampleCount;
        descriptor.storageMode = MTLStorageModePrivate;
        descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
        if (info.usageFlags & XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT) {
            descriptor.usage |= MTLTextureUsageShaderWrite;
        }
        for (std::uint32_t i = 0; i < kSwapchainImageCount; ++i) {
            id<MTLTexture> texture = [device_ newTextureWithDescriptor:descriptor];
            if (!texture) {
                runtime_logf("cannot make a %ux%u swapchain image (format %lld)", info.width, info.height,
                             static_cast<long long>(info.format));
                destroy_images(images);
                return false;
            }
            texture.label = is_depth_format(info.format) ? @"VR depth" : @"VR eyes";
            images[i] = (__bridge_retained void*)texture;
        }
        return true;
    }

    void destroy_images(void* images[kSwapchainImageCount]) override {
        for (std::uint32_t i = 0; i < kSwapchainImageCount; ++i) {
            if (images[i]) {
                CFRelease(images[i]);
                images[i] = nullptr;
            }
        }
    }

    XrResult enumerate_images(void* const images[kSwapchainImageCount], XrSwapchainImageBaseHeader* out) const override {
        auto* metal_images = reinterpret_cast<XrSwapchainImageMetalKHR*>(out);
        for (std::uint32_t i = 0; i < kSwapchainImageCount; ++i) {
            if (metal_images[i].type != XR_TYPE_SWAPCHAIN_IMAGE_METAL_KHR) {
                return XR_ERROR_VALIDATION_FAILURE;
            }
            metal_images[i].texture = images[i];
        }
        return XR_SUCCESS;
    }

    bool copies_after_end_frame() const override { return true; }

    bool start_export(SharedFrames& frames) override {
        // The GPU writes straight into the shared memory through a buffer over it.
        buffer_ = [device_ newBufferWithBytesNoCopy:frames.view()
                                             length:frames.mapped_size()
                                            options:MTLResourceStorageModeShared
                                        deallocator:nil];
        if (!buffer_) {
            runtime_log("frame export: the GPU can't write into the shared memory");
            return false;
        }
        return true;
    }

    void export_frame(SharedFrames& frames, const EyeSource eyes[2], const SharedFrames::Frame& frame) override {
        const int slot = frames.begin_slot();
        if (slot < 0) {
            return;
        }
        id<MTLCommandBuffer> commands = [queue_ commandBuffer];
        commands.label = @"VR frame copy";
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        for (std::uint32_t eye = 0; eye < 2; ++eye) {
            [blit copyFromTexture:(__bridge id<MTLTexture>)eyes[eye].image
                             sourceSlice:eyes[eye].array_index
                             sourceLevel:0
                            sourceOrigin:MTLOriginMake(0, 0, 0)
                              sourceSize:MTLSizeMake(frames.eye_width(), frames.eye_height(), 1)
                                toBuffer:buffer_
                       destinationOffset:frames.slot_pixels_offset(slot) + eye * static_cast<std::uint64_t>(frames.eye_size())
                  destinationBytesPerRow:frames.eye_stride()
                destinationBytesPerImage:frames.eye_size()];
        }
        [blit endEncoding];
        const SharedFrames::Frame copy = frame;
        const std::uint64_t encoded_at = platform::monotonic_ns();
        SharedFrames* shared = &frames;
        [commands addCompletedHandler:^(id<MTLCommandBuffer> done) {
            const auto readback_us = static_cast<std::uint32_t>((platform::monotonic_ns() - encoded_at) / 1000);
            shared->publish(slot, copy, done.status == MTLCommandBufferStatusCompleted, readback_us, 0);
        }];
        [commands commit];
    }

    void stop_export(SharedFrames& frames) override {
        // Copies still in flight publish into the mapping when they land: wait for them (they take
        // a frame or two) before it goes.
        for (int waited = 0; waited < 200 && frames.busy(); ++waited) {
            usleep(1000);
        }
        buffer_ = nil;
    }

private:
    id<MTLCommandQueue> queue_;
    id<MTLDevice> device_;
    id<MTLBuffer> buffer_ = nil;
};

}  // namespace

std::unique_ptr<Graphics> make_metal_graphics(void* command_queue) {
    return std::make_unique<MetalGraphics>((__bridge id<MTLCommandQueue>)command_queue);
}

void* metal_default_device() {
    static id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    return (__bridge void*)device;
}
