#ifdef _WIN32

#include "graphics_d3d12.hpp"

#include "dxgi_adapter.hpp"
#include "runtime_log.hpp"

#include <dxgi1_6.h>
// The interface IDs, which MinGW's headers leave out (as Godot's Direct3D 12 driver includes it).
#include <dxguids/dxguids.h>

#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <thread>

namespace {

constexpr std::int64_t kColorFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

class D3D12Graphics final : public Graphics {
public:
    explicit D3D12Graphics(const XrGraphicsBindingD3D12KHR& binding) : device_(binding.device), queue_(binding.queue) {
        device_->AddRef();
        queue_->AddRef();
        // MinGW's headers hand the LUID back through a parameter.
#if defined(_MSC_VER)
        const LUID luid = device_->GetAdapterLuid();
#else
        LUID luid{};
        device_->GetAdapterLuid(&luid);
#endif
        name_ = dxgi_adapter_name(luid);
    }

    ~D3D12Graphics() override {
        queue_->Release();
        device_->Release();
    }

    std::string device_name() const override { return name_; }

    const std::vector<std::int64_t>& formats() const override {
        static const std::vector<std::int64_t> kFormats = {kColorFormat, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D24_UNORM_S8_UINT,
                                                           DXGI_FORMAT_D32_FLOAT_S8X24_UINT};
        return kFormats;
    }

    bool is_depth_format(std::int64_t format) const override {
        return format == DXGI_FORMAT_D32_FLOAT || format == DXGI_FORMAT_D24_UNORM_S8_UINT || format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    }

    vr_shared::PixelFormat shared_format(std::int64_t format) const override {
        return format == kColorFormat ? vr_shared::PixelFormat::bgra8 : vr_shared::PixelFormat::unknown;
    }

    bool create_images(const XrSwapchainCreateInfo& info, void* images[kSwapchainImageCount]) override {
        const bool depth = is_depth_format(info.format);
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = info.width;
        description.Height = info.height;
        description.DepthOrArraySize = static_cast<UINT16>(info.arraySize);
        description.MipLevels = static_cast<UINT16>(info.mipCount);
        description.Format = static_cast<DXGI_FORMAT>(info.format);
        description.SampleDesc.Count = info.sampleCount;
        description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        description.Flags = depth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (info.usageFlags & XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT) {
            description.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        // Handed to the game in these states, as OpenXR's Direct3D 12 binding asks.
        const D3D12_RESOURCE_STATES state = depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
        for (std::uint32_t i = 0; i < kSwapchainImageCount; ++i) {
            ID3D12Resource* texture = nullptr;
            if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description, state, nullptr, IID_PPV_ARGS(&texture)))) {
                runtime_logf("cannot make a %ux%u swapchain image (format %lld)", info.width, info.height,
                             static_cast<long long>(info.format));
                destroy_images(images);
                return false;
            }
            images[i] = texture;
        }
        {
            std::lock_guard<std::mutex> lock(mip_levels_lock_);
            for (std::uint32_t i = 0; i < kSwapchainImageCount; ++i) {
                mip_levels_[images[i]] = info.mipCount;
            }
        }
        return true;
    }

    void destroy_images(void* images[kSwapchainImageCount]) override {
        for (std::uint32_t i = 0; i < kSwapchainImageCount; ++i) {
            if (images[i]) {
                {
                    std::lock_guard<std::mutex> lock(mip_levels_lock_);
                    mip_levels_.erase(images[i]);
                }
                static_cast<ID3D12Resource*>(images[i])->Release();
                images[i] = nullptr;
            }
        }
    }

    XrResult enumerate_images(void* const images[kSwapchainImageCount], XrSwapchainImageBaseHeader* out) const override {
        auto* d3d12_images = reinterpret_cast<XrSwapchainImageD3D12KHR*>(out);
        for (std::uint32_t i = 0; i < kSwapchainImageCount; ++i) {
            if (d3d12_images[i].type != XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR) {
                return XR_ERROR_VALIDATION_FAILURE;
            }
            d3d12_images[i].texture = static_cast<ID3D12Resource*>(images[i]);
        }
        return XR_SUCCESS;
    }

    bool start_export(SharedFrames& frames) override {
        // Rows in a readback buffer are padded to D3D12_TEXTURE_DATA_PITCH_ALIGNMENT.
        row_pitch_ = (frames.eye_stride() + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) / D3D12_TEXTURE_DATA_PITCH_ALIGNMENT *
                     D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
        eye_bytes_ = static_cast<std::uint64_t>(row_pitch_) * frames.eye_height();
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = 2 * eye_bytes_;
        buffer.Height = 1;
        buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        for (Slot& slot : slots_) {
            if (FAILED(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                        IID_PPV_ARGS(&slot.buffer))) ||
                FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&slot.allocator))) ||
                FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator, nullptr, IID_PPV_ARGS(&slot.list)))) {
                runtime_log("frame export: cannot make the readback buffers");
                return false;
            }
            slot.list->Close();
        }
        if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
            return false;
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
        slot.allocator->Reset();
        slot.list->Reset(slot.allocator, nullptr);
        for (std::uint32_t eye = 0; eye < 2; ++eye) {
            auto* texture = static_cast<ID3D12Resource*>(eyes[eye].image);
            const UINT subresource = eyes[eye].array_index * mip_levels_of(eyes[eye].image);
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = texture;
            barrier.Transition.Subresource = subresource;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            slot.list->ResourceBarrier(1, &barrier);
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = texture;
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            source.SubresourceIndex = subresource;
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = slot.buffer;
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint.Offset = eye * eye_bytes_;
            destination.PlacedFootprint.Footprint = {static_cast<DXGI_FORMAT>(kColorFormat), frames.eye_width(), frames.eye_height(), 1,
                                                     row_pitch_};
            D3D12_BOX box{0, 0, 0, frames.eye_width(), frames.eye_height(), 1};
            slot.list->CopyTextureRegion(&destination, 0, 0, 0, &source, &box);
            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            slot.list->ResourceBarrier(1, &barrier);
        }
        slot.list->Close();
        // After the game's frame on the same queue, so the GPU orders the two. OpenXR lets the
        // runtime use the game's queue during xrEndFrame.
        ID3D12CommandList* lists[] = {slot.list};
        queue_->ExecuteCommandLists(1, lists);
        const std::uint64_t value = ++fence_value_;
        queue_->Signal(fence_, value);
        std::lock_guard<std::mutex> lock(jobs_lock_);
        jobs_.push_back(Job{index, frame, platform::monotonic_ns(), value, &frames});
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
            if (slot.list) {
                slot.list->Release();
            }
            if (slot.allocator) {
                slot.allocator->Release();
            }
            if (slot.buffer) {
                slot.buffer->Release();
            }
            slot = Slot{};
        }
        if (fence_) {
            fence_->Release();
            fence_ = nullptr;
        }
    }

private:
    struct Slot {
        ID3D12Resource* buffer = nullptr;
        ID3D12CommandAllocator* allocator = nullptr;
        ID3D12GraphicsCommandList* list = nullptr;
    };

    struct Job {
        int slot = 0;
        SharedFrames::Frame frame;
        std::uint64_t submitted_at = 0;
        std::uint64_t fence_value = 0;
        SharedFrames* frames = nullptr;
    };

    // The worker: waits for each copy to land, then moves it into the shared slot (dropping the
    // row padding) and publishes it, so the game's thread never waits for the GPU.
    void publish_copies() {
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(jobs_lock_);
                jobs_changed_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (jobs_.empty()) {
                    break;
                }
                job = jobs_.front();
                jobs_.pop_front();
            }
            bool landed = fence_->GetCompletedValue() >= job.fence_value;
            if (!landed && SUCCEEDED(fence_->SetEventOnCompletion(job.fence_value, event))) {
                landed = WaitForSingleObject(event, 1000) == WAIT_OBJECT_0;
            }
            const std::uint64_t landed_at = platform::monotonic_ns();
            Slot& slot = slots_[job.slot];
            void* mapped = nullptr;
            const D3D12_RANGE read{0, static_cast<SIZE_T>(2 * eye_bytes_)};
            if (landed && SUCCEEDED(slot.buffer->Map(0, &read, &mapped))) {
                std::uint8_t* out = job.frames->slot_pixels(job.slot);
                const std::uint32_t stride = job.frames->eye_stride();
                for (std::uint32_t eye = 0; eye < 2; ++eye) {
                    const auto* source = static_cast<const std::uint8_t*>(mapped) + eye * eye_bytes_;
                    for (std::uint32_t row = 0; row < job.frames->eye_height(); ++row) {
                        std::memcpy(out + (eye * job.frames->eye_height() + row) * static_cast<std::uint64_t>(stride),
                                    source + row * static_cast<std::uint64_t>(row_pitch_), stride);
                    }
                }
                const D3D12_RANGE written{0, 0};
                slot.buffer->Unmap(0, &written);
            } else {
                landed = false;
            }
            job.frames->publish(job.slot, job.frame, landed, static_cast<std::uint32_t>((landed_at - job.submitted_at) / 1000),
                                static_cast<std::uint32_t>((platform::monotonic_ns() - landed_at) / 1000));
        }
        CloseHandle(event);
    }

    ID3D12Device* device_;
    ID3D12CommandQueue* queue_;
    std::string name_;
    // Each swapchain image's mip count (colour and depth swapchains can differ), for the subresource
    // of an eye's array layer.
    std::unordered_map<void*, UINT> mip_levels_;
    std::mutex mip_levels_lock_;
    UINT mip_levels_of(void* image) {
        std::lock_guard<std::mutex> lock(mip_levels_lock_);
        const auto found = mip_levels_.find(image);
        return found == mip_levels_.end() ? 1 : found->second;
    }
    UINT row_pitch_ = 0;
    std::uint64_t eye_bytes_ = 0;
    Slot slots_[vr_shared::kSlotCount];
    ID3D12Fence* fence_ = nullptr;
    std::uint64_t fence_value_ = 0;

    std::thread worker_;
    std::mutex jobs_lock_;
    std::condition_variable jobs_changed_;
    std::deque<Job> jobs_;
    bool stopping_ = false;
};

}  // namespace

std::unique_ptr<Graphics> make_d3d12_graphics(const XrGraphicsBindingD3D12KHR& binding) {
    if (!binding.device || !binding.queue) {
        return nullptr;
    }
    return std::make_unique<D3D12Graphics>(binding);
}

XrResult d3d12_graphics_requirements(XrGraphicsRequirementsD3D12KHR* requirements) {
    if (!requirements) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    if (!dxgi_high_performance_adapter(requirements->adapterLuid)) {
        return XR_ERROR_RUNTIME_FAILURE;
    }
    requirements->minFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    return XR_SUCCESS;
}

#endif  // _WIN32
