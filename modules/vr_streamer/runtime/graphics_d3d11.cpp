#ifdef _WIN32

#include "graphics_d3d11.hpp"

#include "dxgi_adapter.hpp"
#include "runtime_log.hpp"

#include <d3d11_4.h>
// The interface IDs, which MinGW's headers leave out.
#include <dxguids/dxguids.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <thread>

namespace {

constexpr std::int64_t kColorFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
// The longest a copy may take to land before the frame is dropped.
constexpr auto kLongestCopy = std::chrono::seconds(1);

// The game's immediate context, held while this side uses it: Direct3D 11's contexts aren't
// thread-safe, and the copies are read back on a thread of the runtime's own.
class ContextLock {
public:
    explicit ContextLock(ID3D11Multithread* multithread) : multithread_(multithread) {
        if (multithread_) {
            multithread_->Enter();
        }
    }
    ~ContextLock() {
        if (multithread_) {
            multithread_->Leave();
        }
    }
    ContextLock(const ContextLock&) = delete;
    ContextLock& operator=(const ContextLock&) = delete;

private:
    ID3D11Multithread* multithread_;
};

template <typename T>
void release(T*& object) {
    if (object) {
        object->Release();
        object = nullptr;
    }
}

class D3D11Graphics final : public Graphics {
public:
    explicit D3D11Graphics(ID3D11Device* device) : device_(device) {
        device_->AddRef();
        device_->GetImmediateContext(&context_);
        // The game's thread and the runtime's both use the immediate context from now on.
        if (SUCCEEDED(context_->QueryInterface(IID_PPV_ARGS(&multithread_)))) {
            multithread_->SetMultithreadProtected(TRUE);
        } else {
            runtime_log("Direct3D 11: no ID3D11Multithread, so frames are read back on the game's thread");
        }
        IDXGIDevice* dxgi = nullptr;
        if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&dxgi)))) {
            IDXGIAdapter* adapter = nullptr;
            if (SUCCEEDED(dxgi->GetAdapter(&adapter))) {
                DXGI_ADAPTER_DESC description{};
                adapter->GetDesc(&description);
                name_ = dxgi_adapter_name(description.AdapterLuid);
                adapter->Release();
            }
            dxgi->Release();
        }
    }

    ~D3D11Graphics() override {
        release(multithread_);
        release(context_);
        release(device_);
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
        D3D11_TEXTURE2D_DESC description{};
        description.Width = info.width;
        description.Height = info.height;
        description.MipLevels = info.mipCount;
        description.ArraySize = info.arraySize;
        description.Format = static_cast<DXGI_FORMAT>(info.format);
        description.SampleDesc.Count = info.sampleCount;
        description.Usage = D3D11_USAGE_DEFAULT;
        if (info.usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) {
            description.BindFlags |= D3D11_BIND_RENDER_TARGET;
        }
        if (info.usageFlags & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            description.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
        }
        if (info.usageFlags & XR_SWAPCHAIN_USAGE_SAMPLED_BIT) {
            description.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        }
        if (info.usageFlags & XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT) {
            description.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
        }
        if (description.BindFlags == 0) {
            description.BindFlags = is_depth_format(info.format) ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_RENDER_TARGET;
        }
        for (std::uint32_t i = 0; i < kSwapchainImageCount; ++i) {
            ID3D11Texture2D* texture = nullptr;
            if (FAILED(device_->CreateTexture2D(&description, nullptr, &texture))) {
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
                static_cast<ID3D11Texture2D*>(images[i])->Release();
                images[i] = nullptr;
            }
        }
    }

    XrResult enumerate_images(void* const images[kSwapchainImageCount], XrSwapchainImageBaseHeader* out) const override {
        auto* d3d11_images = reinterpret_cast<XrSwapchainImageD3D11KHR*>(out);
        for (std::uint32_t i = 0; i < kSwapchainImageCount; ++i) {
            if (d3d11_images[i].type != XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR) {
                return XR_ERROR_VALIDATION_FAILURE;
            }
            d3d11_images[i].texture = static_cast<ID3D11Texture2D*>(images[i]);
        }
        return XR_SUCCESS;
    }

    bool start_export(SharedFrames& frames) override {
        D3D11_TEXTURE2D_DESC staging{};
        staging.Width = frames.eye_width();
        staging.Height = frames.eye_height();
        staging.MipLevels = 1;
        staging.ArraySize = 1;
        staging.Format = static_cast<DXGI_FORMAT>(kColorFormat);
        staging.SampleDesc.Count = 1;
        staging.Usage = D3D11_USAGE_STAGING;
        staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        D3D11_QUERY_DESC landed{};
        landed.Query = D3D11_QUERY_EVENT;
        for (Slot& slot : slots_) {
            for (ID3D11Texture2D*& eye : slot.eyes) {
                if (FAILED(device_->CreateTexture2D(&staging, nullptr, &eye))) {
                    runtime_log("frame export: cannot make the readback textures");
                    return false;
                }
            }
            if (FAILED(device_->CreateQuery(&landed, &slot.landed))) {
                return false;
            }
        }
        stopping_ = false;
        if (multithread_) {
            worker_ = std::thread([this] { publish_copies(); });
        }
        return true;
    }

    void export_frame(SharedFrames& frames, const EyeSource eyes[2], const SharedFrames::Frame& frame) override {
        const int index = frames.begin_slot();
        if (index < 0) {
            return;
        }
        Slot& slot = slots_[index];
        {
            ContextLock lock(multithread_);
            for (std::uint32_t eye = 0; eye < 2; ++eye) {
                const UINT subresource = D3D11CalcSubresource(0, eyes[eye].array_index, mip_levels_of(eyes[eye].image));
                const D3D11_BOX box{0, 0, 0, frames.eye_width(), frames.eye_height(), 1};
                context_->CopySubresourceRegion(slot.eyes[eye], 0, 0, 0, 0, static_cast<ID3D11Texture2D*>(eyes[eye].image), subresource,
                                                &box);
            }
            context_->End(slot.landed);
            // Starts the copies now rather than at the game's next flush.
            context_->Flush();
        }
        const Job job{index, frame, platform::monotonic_ns(), &frames};
        if (!multithread_) {
            publish(job, true);
            return;
        }
        std::lock_guard<std::mutex> lock(jobs_lock_);
        jobs_.push_back(job);
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
            release(slot.eyes[0]);
            release(slot.eyes[1]);
            release(slot.landed);
        }
    }

private:
    struct Slot {
        ID3D11Texture2D* eyes[2] = {};
        ID3D11Query* landed = nullptr;
    };

    struct Job {
        int slot = 0;
        SharedFrames::Frame frame;
        std::uint64_t submitted_at = 0;
        SharedFrames* frames = nullptr;
    };

    // The worker: waits for each copy to land, holding the context only to ask, so the game's
    // thread never waits for the GPU on this side's account.
    void publish_copies() {
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
            publish(job, false);
        }
    }

    // Reads a slot's copy into the shared file and publishes it. `wait` (no worker): the game's
    // own thread, which then waits for the copy as Direct3D 11 does.
    void publish(const Job& job, bool wait) {
        Slot& slot = slots_[job.slot];
        bool landed = wait;
        const auto give_up = std::chrono::steady_clock::now() + kLongestCopy;
        while (!landed && std::chrono::steady_clock::now() < give_up) {
            {
                ContextLock lock(multithread_);
                landed = context_->GetData(slot.landed, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
            }
            if (!landed) {
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        }
        const std::uint64_t landed_at = platform::monotonic_ns();
        D3D11_MAPPED_SUBRESOURCE mapped[2] = {};
        if (landed) {
            ContextLock lock(multithread_);
            for (std::uint32_t eye = 0; eye < 2 && landed; ++eye) {
                landed = SUCCEEDED(context_->Map(slot.eyes[eye], 0, D3D11_MAP_READ, wait ? 0 : D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped[eye]));
                if (!landed && eye == 1) {
                    context_->Unmap(slot.eyes[0], 0);
                }
            }
        }
        if (landed) {
            // Mapped, the copy needs no context: the game carries on meanwhile.
            std::uint8_t* out = job.frames->slot_pixels(job.slot);
            const std::uint32_t stride = job.frames->eye_stride();
            for (std::uint32_t eye = 0; eye < 2; ++eye) {
                const auto* source = static_cast<const std::uint8_t*>(mapped[eye].pData);
                for (std::uint32_t row = 0; row < job.frames->eye_height(); ++row) {
                    std::memcpy(out + (eye * job.frames->eye_height() + row) * static_cast<std::uint64_t>(stride),
                                source + row * static_cast<std::uint64_t>(mapped[eye].RowPitch), stride);
                }
            }
            ContextLock lock(multithread_);
            context_->Unmap(slot.eyes[0], 0);
            context_->Unmap(slot.eyes[1], 0);
        }
        job.frames->publish(job.slot, job.frame, landed, static_cast<std::uint32_t>((landed_at - job.submitted_at) / 1000),
                            static_cast<std::uint32_t>((platform::monotonic_ns() - landed_at) / 1000));
    }

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    ID3D11Multithread* multithread_ = nullptr;
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
    Slot slots_[vr_shared::kSlotCount];

    std::thread worker_;
    std::mutex jobs_lock_;
    std::condition_variable jobs_changed_;
    std::deque<Job> jobs_;
    bool stopping_ = false;
};

}  // namespace

std::unique_ptr<Graphics> make_d3d11_graphics(const XrGraphicsBindingD3D11KHR& binding) {
    if (!binding.device) {
        return nullptr;
    }
    return std::make_unique<D3D11Graphics>(binding.device);
}

XrResult d3d11_graphics_requirements(XrGraphicsRequirementsD3D11KHR* requirements) {
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
