#include "graphics_vulkan.hpp"

#include "runtime_log.hpp"

#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

// XR_KHR_vulkan_enable2. The runtime creates the game's Vulkan instance and device through the
// loader function the game passes, so it never links against Vulkan itself.

namespace {

PFN_vkGetInstanceProcAddr g_get_instance_proc_addr = nullptr;
VkInstance g_vk_instance = VK_NULL_HANDLE;  // the game's, made in vulkan_create_instance

template <typename T>
T instance_function(VkInstance instance, const char* name) {
    return g_get_instance_proc_addr ? reinterpret_cast<T>(g_get_instance_proc_addr(instance, name)) : nullptr;
}

constexpr std::int64_t kColorFormat = VK_FORMAT_B8G8R8A8_SRGB;

class VulkanGraphics final : public Graphics {
public:
    explicit VulkanGraphics(const XrGraphicsBindingVulkanKHR& binding)
        : instance_(binding.instance), physical_device_(binding.physicalDevice), device_(binding.device) {
        auto get_device_proc_addr = instance_function<PFN_vkGetDeviceProcAddr>(instance_, "vkGetDeviceProcAddr");
#define LOAD(name) name = reinterpret_cast<PFN_##name>(get_device_proc_addr(device_, #name))
        LOAD(vkGetDeviceQueue);
        LOAD(vkCreateImage);
        LOAD(vkDestroyImage);
        LOAD(vkGetImageMemoryRequirements);
        LOAD(vkAllocateMemory);
        LOAD(vkFreeMemory);
        LOAD(vkBindImageMemory);
        LOAD(vkCreateBuffer);
        LOAD(vkDestroyBuffer);
        LOAD(vkGetBufferMemoryRequirements);
        LOAD(vkBindBufferMemory);
        LOAD(vkMapMemory);
        LOAD(vkUnmapMemory);
        LOAD(vkInvalidateMappedMemoryRanges);
        LOAD(vkCreateCommandPool);
        LOAD(vkDestroyCommandPool);
        LOAD(vkAllocateCommandBuffers);
        LOAD(vkBeginCommandBuffer);
        LOAD(vkEndCommandBuffer);
        LOAD(vkResetCommandBuffer);
        LOAD(vkCmdPipelineBarrier);
        LOAD(vkCmdCopyImageToBuffer);
        LOAD(vkQueueSubmit);
        LOAD(vkCreateFence);
        LOAD(vkDestroyFence);
        LOAD(vkWaitForFences);
        LOAD(vkResetFences);
#undef LOAD
        vkGetPhysicalDeviceMemoryProperties = instance_function<PFN_vkGetPhysicalDeviceMemoryProperties>(instance_, "vkGetPhysicalDeviceMemoryProperties");
        auto get_properties = instance_function<PFN_vkGetPhysicalDeviceProperties>(instance_, "vkGetPhysicalDeviceProperties");
        VkPhysicalDeviceProperties properties{};
        get_properties(physical_device_, &properties);
        name_ = properties.deviceName;
        vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties_);
        vkGetDeviceQueue(device_, binding.queueFamilyIndex, binding.queueIndex, &queue_);

        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = binding.queueFamilyIndex;
        vkCreateCommandPool(device_, &pool_info, nullptr, &pool_);
    }

    ~VulkanGraphics() override {
        if (pool_) {
            vkDestroyCommandPool(device_, pool_, nullptr);
        }
    }

    std::string device_name() const override { return name_; }

    const std::vector<std::int64_t>& formats() const override {
        static const std::vector<std::int64_t> kFormats = {kColorFormat, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT,
                                                           VK_FORMAT_D32_SFLOAT_S8_UINT};
        return kFormats;
    }

    bool is_depth_format(std::int64_t format) const override {
        return format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
    }

    vr_shared::PixelFormat shared_format(std::int64_t format) const override {
        return format == kColorFormat ? vr_shared::PixelFormat::bgra8 : vr_shared::PixelFormat::unknown;
    }

    bool create_images(const XrSwapchainCreateInfo& info, void* images[kSwapchainImageCount]) override {
        const bool depth = is_depth_format(info.format);
        VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = static_cast<VkFormat>(info.format);
        image_info.extent = {info.width, info.height, 1};
        image_info.mipLevels = info.mipCount;
        image_info.arrayLayers = info.arraySize;
        image_info.samples = static_cast<VkSampleCountFlagBits>(info.sampleCount);
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                           (depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        if (info.usageFlags & XR_SWAPCHAIN_USAGE_INPUT_ATTACHMENT_BIT_KHR) {
            image_info.usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
        }
        if (info.usageFlags & XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT) {
            image_info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        }
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        for (std::uint32_t i = 0; i < kSwapchainImageCount; ++i) {
            auto* image = new Image();
            image->depth = depth;
            image->layers = info.arraySize;
            image->mips = info.mipCount;
            images[i] = image;
            if (vkCreateImage(device_, &image_info, nullptr, &image->image) != VK_SUCCESS) {
                runtime_logf("cannot make a %ux%u swapchain image (format %lld)", info.width, info.height,
                             static_cast<long long>(info.format));
                destroy_images(images);
                return false;
            }
            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(device_, image->image, &requirements);
            if (!allocate(requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image->memory) ||
                vkBindImageMemory(device_, image->image, image->memory, 0) != VK_SUCCESS) {
                runtime_log("cannot give a swapchain image its memory");
                destroy_images(images);
                return false;
            }
        }
        return true;
    }

    void destroy_images(void* images[kSwapchainImageCount]) override {
        for (std::uint32_t i = 0; i < kSwapchainImageCount; ++i) {
            auto* image = static_cast<Image*>(images[i]);
            if (!image) {
                continue;
            }
            if (image->image) {
                vkDestroyImage(device_, image->image, nullptr);
            }
            if (image->memory) {
                vkFreeMemory(device_, image->memory, nullptr);
            }
            delete image;
            images[i] = nullptr;
        }
    }

    XrResult enumerate_images(void* const images[kSwapchainImageCount], XrSwapchainImageBaseHeader* out) const override {
        auto* vulkan_images = reinterpret_cast<XrSwapchainImageVulkanKHR*>(out);
        for (std::uint32_t i = 0; i < kSwapchainImageCount; ++i) {
            if (vulkan_images[i].type != XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR) {
                return XR_ERROR_VALIDATION_FAILURE;
            }
            vulkan_images[i].image = static_cast<Image*>(images[i])->image;
        }
        return XR_SUCCESS;
    }

    void prepare_image(void* image_handle) override {
        // Handed to the game in the attachment layout, as OpenXR's Vulkan binding asks.
        auto* image = static_cast<Image*>(image_handle);
        if (image->prepared) {
            return;
        }
        image->prepared = true;
        VkCommandBuffer commands = one_time_commands();
        if (!commands) {
            return;
        }
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = attachment_layout(*image);
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image->image;
        barrier.subresourceRange = {aspect(*image), 0, image->mips, 0, image->layers};
        barrier.dstAccessMask = image->depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &barrier);
        submit_one_time(commands);
    }

    bool start_export(SharedFrames& frames) override {
        const VkDeviceSize size = 2ULL * frames.eye_size();
        for (Slot& slot : slots_) {
            VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            buffer_info.size = size;
            buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            if (vkCreateBuffer(device_, &buffer_info, nullptr, &slot.buffer) != VK_SUCCESS) {
                return false;
            }
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(device_, slot.buffer, &requirements);
            // Cached memory reads back much faster; coherent if there's no cached kind.
            if (!allocate(requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, slot.memory) &&
                !allocate(requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, slot.memory)) {
                runtime_log("frame export: no memory the CPU can read back from");
                return false;
            }
            vkBindBufferMemory(device_, slot.buffer, slot.memory, 0);
            vkMapMemory(device_, slot.memory, 0, VK_WHOLE_SIZE, 0, &slot.mapped);
            VkCommandBufferAllocateInfo allocate_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocate_info.commandPool = pool_;
            allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate_info.commandBufferCount = 1;
            vkAllocateCommandBuffers(device_, &allocate_info, &slot.commands);
            VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            vkCreateFence(device_, &fence_info, nullptr, &slot.fence);
        }
        stopping_ = false;
        worker_ = std::thread([this] { publish_copies(); });
        return true;
    }

    void export_frame(SharedFrames& frames, const EyeSource eyes[2], const SharedFrames::Frame& frame) override {
        const int index = frames.begin_slot();
        if (index < 0) {
            return;
        }
        Slot& slot = slots_[index];
        vkResetFences(device_, 1, &slot.fence);
        vkResetCommandBuffer(slot.commands, 0);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(slot.commands, &begin);
        for (std::uint32_t eye = 0; eye < 2; ++eye) {
            const Image& image = *static_cast<Image*>(eyes[eye].image);
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image.image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, eyes[eye].array_index, 1};
            barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(slot.commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                                 nullptr, 0, nullptr, 1, &barrier);
            VkBufferImageCopy region{};
            region.bufferOffset = eye * static_cast<VkDeviceSize>(frames.eye_size());
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, eyes[eye].array_index, 1};
            region.imageExtent = {frames.eye_width(), frames.eye_height(), 1};
            vkCmdCopyImageToBuffer(slot.commands, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot.buffer, 1, &region);
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            vkCmdPipelineBarrier(slot.commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                                 nullptr, 0, nullptr, 1, &barrier);
        }
        vkEndCommandBuffer(slot.commands);
        // After the game's frame on the same queue, so the GPU orders the two. OpenXR lets the
        // runtime use the game's queue during xrEndFrame.
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &slot.commands;
        const bool submitted = vkQueueSubmit(queue_, 1, &submit, slot.fence) == VK_SUCCESS;
        std::lock_guard<std::mutex> lock(jobs_lock_);
        jobs_.push_back(Job{index, frame, platform::monotonic_ns(), submitted, &frames});
        jobs_changed_.notify_one();
    }

    void stop_export(SharedFrames&) override {
        {
            std::lock_guard<std::mutex> lock(jobs_lock_);
            stopping_ = true;
            jobs_changed_.notify_one();
        }
        if (worker_.joinable()) {
            worker_.join();
        }
        for (Slot& slot : slots_) {
            if (slot.fence) {
                vkDestroyFence(device_, slot.fence, nullptr);
            }
            if (slot.buffer) {
                vkDestroyBuffer(device_, slot.buffer, nullptr);
            }
            if (slot.memory) {
                vkFreeMemory(device_, slot.memory, nullptr);
            }
            slot = Slot{};
        }
    }

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        bool depth = false;
        bool prepared = false;
        std::uint32_t layers = 1;
        std::uint32_t mips = 1;
    };

    struct Slot {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkCommandBuffer commands = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
    };

    struct Job {
        int slot = 0;
        SharedFrames::Frame frame;
        std::uint64_t submitted_at = 0;
        bool submitted = false;
        SharedFrames* frames = nullptr;
    };

    static VkImageLayout attachment_layout(const Image& image) {
        return image.depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    static VkImageAspectFlags aspect(const Image& image) {
        return image.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    }

    bool allocate(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags wanted, VkDeviceMemory& out) {
        for (std::uint32_t type = 0; type < memory_properties_.memoryTypeCount; ++type) {
            if ((requirements.memoryTypeBits & (1u << type)) &&
                (memory_properties_.memoryTypes[type].propertyFlags & wanted) == wanted) {
                VkMemoryAllocateInfo allocate_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                allocate_info.allocationSize = requirements.size;
                allocate_info.memoryTypeIndex = type;
                return vkAllocateMemory(device_, &allocate_info, nullptr, &out) == VK_SUCCESS;
            }
        }
        return false;
    }

    VkCommandBuffer one_time_commands() {
        VkCommandBufferAllocateInfo allocate_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate_info.commandPool = pool_;
        allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate_info.commandBufferCount = 1;
        VkCommandBuffer commands = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device_, &allocate_info, &commands) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commands, &begin);
        return commands;
    }

    // Runs commands now and waits for them (only when a swapchain image is first acquired).
    void submit_one_time(VkCommandBuffer commands) {
        vkEndCommandBuffer(commands);
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        vkCreateFence(device_, &fence_info, nullptr, &fence);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commands;
        if (vkQueueSubmit(queue_, 1, &submit, fence) == VK_SUCCESS) {
            vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
        }
        vkDestroyFence(device_, fence, nullptr);
    }

    // The worker: waits for each copy to land, then moves it into the shared slot and publishes
    // it, so the game's thread never waits for the GPU.
    void publish_copies() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(jobs_lock_);
                jobs_changed_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (jobs_.empty()) {
                    return;  // stopping, with nothing left in flight
                }
                job = jobs_.front();
                jobs_.pop_front();
            }
            Slot& slot = slots_[job.slot];
            const bool landed = job.submitted && vkWaitForFences(device_, 1, &slot.fence, VK_TRUE, 1000000000ULL) == VK_SUCCESS;
            const std::uint64_t landed_at = platform::monotonic_ns();
            if (landed) {
                VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
                range.memory = slot.memory;
                range.size = VK_WHOLE_SIZE;
                vkInvalidateMappedMemoryRanges(device_, 1, &range);
                std::memcpy(job.frames->slot_pixels(job.slot), slot.mapped, 2ULL * job.frames->eye_size());
            }
            job.frames->publish(job.slot, job.frame, landed, static_cast<std::uint32_t>((landed_at - job.submitted_at) / 1000),
                                static_cast<std::uint32_t>((platform::monotonic_ns() - landed_at) / 1000));
        }
    }

    VkInstance instance_;
    VkPhysicalDevice physical_device_;
    VkDevice device_;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory_properties_{};
    std::string name_;
    Slot slots_[vr_shared::kSlotCount];

    std::thread worker_;
    std::mutex jobs_lock_;
    std::condition_variable jobs_changed_;
    std::deque<Job> jobs_;
    bool stopping_ = false;

#define DECLARE(name) PFN_##name name = nullptr
    DECLARE(vkGetDeviceQueue);
    DECLARE(vkCreateImage);
    DECLARE(vkDestroyImage);
    DECLARE(vkGetImageMemoryRequirements);
    DECLARE(vkAllocateMemory);
    DECLARE(vkFreeMemory);
    DECLARE(vkBindImageMemory);
    DECLARE(vkCreateBuffer);
    DECLARE(vkDestroyBuffer);
    DECLARE(vkGetBufferMemoryRequirements);
    DECLARE(vkBindBufferMemory);
    DECLARE(vkMapMemory);
    DECLARE(vkUnmapMemory);
    DECLARE(vkInvalidateMappedMemoryRanges);
    DECLARE(vkCreateCommandPool);
    DECLARE(vkDestroyCommandPool);
    DECLARE(vkAllocateCommandBuffers);
    DECLARE(vkBeginCommandBuffer);
    DECLARE(vkEndCommandBuffer);
    DECLARE(vkResetCommandBuffer);
    DECLARE(vkCmdPipelineBarrier);
    DECLARE(vkCmdCopyImageToBuffer);
    DECLARE(vkQueueSubmit);
    DECLARE(vkCreateFence);
    DECLARE(vkDestroyFence);
    DECLARE(vkWaitForFences);
    DECLARE(vkResetFences);
    DECLARE(vkGetPhysicalDeviceMemoryProperties);
#undef DECLARE
};

}  // namespace

std::unique_ptr<Graphics> make_vulkan_graphics(const XrGraphicsBindingVulkanKHR& binding) {
    if (!g_get_instance_proc_addr) {
        runtime_log("Vulkan: the game didn't create its instance through xrCreateVulkanInstanceKHR");
        return nullptr;
    }
    return std::make_unique<VulkanGraphics>(binding);
}

XrResult vulkan_graphics_requirements(XrGraphicsRequirementsVulkanKHR* requirements) {
    if (!requirements) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    requirements->minApiVersionSupported = XR_MAKE_VERSION(1, 0, 0);
    requirements->maxApiVersionSupported = XR_MAKE_VERSION(1, 4, 0);
    return XR_SUCCESS;
}

XrResult vulkan_create_instance(const XrVulkanInstanceCreateInfoKHR* info, VkInstance* instance, VkResult* vk_result) {
    if (!info || !info->pfnGetInstanceProcAddr || !info->vulkanCreateInfo || !instance || !vk_result) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    g_get_instance_proc_addr = info->pfnGetInstanceProcAddr;
    auto create_instance = instance_function<PFN_vkCreateInstance>(VK_NULL_HANDLE, "vkCreateInstance");
    *vk_result = create_instance(info->vulkanCreateInfo, info->vulkanAllocator, instance);
    if (*vk_result == VK_SUCCESS) {
        g_vk_instance = *instance;
    }
    return *vk_result == VK_SUCCESS ? XR_SUCCESS : XR_ERROR_RUNTIME_FAILURE;
}

XrResult vulkan_graphics_device(const XrVulkanGraphicsDeviceGetInfoKHR* info, VkPhysicalDevice* device) {
    if (!info || !device || !g_get_instance_proc_addr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    auto enumerate = instance_function<PFN_vkEnumeratePhysicalDevices>(info->vulkanInstance, "vkEnumeratePhysicalDevices");
    auto get_properties = instance_function<PFN_vkGetPhysicalDeviceProperties>(info->vulkanInstance, "vkGetPhysicalDeviceProperties");
    std::uint32_t count = 0;
    enumerate(info->vulkanInstance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    enumerate(info->vulkanInstance, &count, devices.data());
    if (devices.empty()) {
        return XR_ERROR_RUNTIME_FAILURE;
    }
    // The discrete GPU if there is one: it draws the game and encodes the video.
    *device = devices[0];
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties properties{};
        get_properties(candidate, &properties);
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            *device = candidate;
            break;
        }
    }
    return XR_SUCCESS;
}

XrResult vulkan_create_device(const XrVulkanDeviceCreateInfoKHR* info, VkDevice* device, VkResult* vk_result) {
    if (!info || !info->pfnGetInstanceProcAddr || !info->vulkanCreateInfo || !device || !vk_result) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    // vkCreateDevice comes from the instance the game made through xrCreateVulkanInstanceKHR.
    auto create_device = reinterpret_cast<PFN_vkCreateDevice>(info->pfnGetInstanceProcAddr(g_vk_instance, "vkCreateDevice"));
    if (!create_device) {
        return XR_ERROR_RUNTIME_FAILURE;
    }
    *vk_result = create_device(info->vulkanPhysicalDevice, info->vulkanCreateInfo, info->vulkanAllocator, device);
    return *vk_result == VK_SUCCESS ? XR_SUCCESS : XR_ERROR_RUNTIME_FAILURE;
}
