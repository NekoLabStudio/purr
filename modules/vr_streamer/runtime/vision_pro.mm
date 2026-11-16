#include "vision_pro.hpp"

#include "runtime_log.hpp"
#include "shared_frames.hpp"
#include "vision_pro_bridge.h"
#include "vr_shared.hpp"

#import <ARKit/ARKit.h>
#import <CompositorServices/CompositorServices.h>
#import <Metal/Metal.h>
#import <dispatch/dispatch.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <thread>
#include <unistd.h>

// Draws one eye of the game's frame into a drawable's view: each pixel of the view looks up the
// direction it shows (its tangents) in the game's frame, which may have been drawn with another
// field of view. Outside the game's frame is black.
static const char *kShader = R"(
#include <metal_stdlib>
using namespace metal;

struct Mapping {
    float4 view;  // the view's left, right, top and bottom tangents (all positive)
    float4 game;  // the game's eye: tangents of its left, right, up and down angles (signed)
};

struct Varyings {
    float4 position [[position]];
    float2 tangent;
};

vertex Varyings eye_vertex(uint id [[vertex_id]], constant Mapping &m [[buffer(0)]]) {
    const float2 uv = float2((id << 1) & 2, id & 2);  // a triangle covering the view
    Varyings out;
    out.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    out.tangent = float2(-m.view.x + uv.x * (m.view.x + m.view.y), m.view.z - uv.y * (m.view.z + m.view.w));
    return out;
}

fragment float4 eye_fragment(Varyings in [[stage_in]], constant Mapping &m [[buffer(0)]], texture2d<float> eye [[texture(0)]]) {
    const float2 uv = float2((in.tangent.x - m.game.x) / (m.game.y - m.game.x), (m.game.z - in.tangent.y) / (m.game.z - m.game.w));
    if (any(uv < 0.0) || any(uv > 1.0)) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    constexpr sampler linear(filter::linear, address::clamp_to_edge);
    return eye.sample(linear, uv);
}
)";

namespace {

struct Mapping {
    simd_float4 view;
    simd_float4 game;
};

// The device anchors the game was given, by the tracking timestamp it got with them: a frame is
// presented with the anchor it was drawn from, so the Vision Pro corrects for the head's
// movement since.
constexpr size_t kAnchorHistory = 32;

std::mutex g_lock;
std::thread g_thread;  // the presenter, while a space is open
std::atomic<bool> g_stop{false};
std::atomic<std::uint32_t> g_recenter_generation{0};
bool g_watching = false;
// The space was asked for since VR started or a Vision Pro last left: asked once, so a player
// who closes or declines it isn't asked again until then.
bool g_asked = false;
vision_pro::Hooks g_hooks;
bool watching() {
    std::lock_guard<std::mutex> lock(g_lock);
    return g_watching;
}

// The hooks run here, never on the presenter's thread, which stop() waits for, and nothing waits
// for this queue (the hooks take the runtime's lock, which stop()'s caller may hold).
dispatch_queue_t hooks_queue() {
    static dispatch_queue_t queue = dispatch_queue_create("purr.vision_pro.hooks", DISPATCH_QUEUE_SERIAL);
    return queue;
}

std::uint64_t unix_ns() {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return static_cast<std::uint64_t>(now.tv_sec) * 1000000000ULL + static_cast<std::uint64_t>(now.tv_nsec);
}

// The shared frames, mapped as the streamer maps them.
struct Shared {
    void *view = nullptr;
    size_t size = 0;
    id<MTLBuffer> buffer = nil;

    vr_shared::SharedHeader *header() const { return static_cast<vr_shared::SharedHeader *>(view); }
    std::uint8_t *bytes() const { return static_cast<std::uint8_t *>(view); }

    bool open(id<MTLDevice> device) {
        const std::string path = SharedFrames::shared_path();
        const int file = ::open(path.c_str(), O_RDWR);
        if (file < 0) {
            return false;
        }
        struct stat info;
        if (fstat(file, &info) != 0 || info.st_size < static_cast<off_t>(vr_shared::kFirstSlotOffset)) {
            ::close(file);
            return false;
        }
        size = static_cast<size_t>(info.st_size);
        view = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, file, 0);
        ::close(file);
        if (view == MAP_FAILED) {
            view = nullptr;
            return false;
        }
        if (header()->magic != vr_shared::kMagic || header()->version != vr_shared::kVersion || header()->total_size_bytes > size) {
            close();
            return false;
        }
        // The GPU reads the pixels straight from the mapping (whole pages, as the runtime makes it).
        buffer = [device newBufferWithBytesNoCopy:view length:size options:MTLResourceStorageModeShared deallocator:nil];
        if (!buffer) {
            runtime_log("Vision Pro: cannot read the frames on the GPU");
            close();
            return false;
        }
        return true;
    }

    void close() {
        buffer = nil;
        if (view) {
            munmap(view, size);
        }
        view = nullptr;
        size = 0;
    }

    // Whether the runtime still publishes into this mapping (it makes a new one when the eye
    // size changes, and none once VR stops).
    bool alive() const { return view && header()->magic == vr_shared::kMagic; }

    void write_host_state(const vr_shared::HostState &state) {
        auto *target = reinterpret_cast<vr_shared::HostState *>(bytes() + vr_shared::kHostStateOffset);
        const std::uint32_t sequence = target->sequence.load(std::memory_order_relaxed);
        target->sequence.store(sequence + 1, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_release);
        target->recenter_generation = state.recenter_generation;
        target->tracking_timestamp_ns = state.tracking_timestamp_ns;
        target->written_at_unix_ns = unix_ns();
        std::memcpy(target->head_orientation, state.head_orientation, sizeof(state.head_orientation));
        std::memcpy(target->head_position, state.head_position, sizeof(state.head_position));
        target->has_pose = state.has_pose;
        std::memcpy(target->fov, state.fov, sizeof(state.fov));
        target->ipd_m = state.ipd_m;
        target->has_views = state.has_views;
        std::atomic_thread_fence(std::memory_order_release);
        target->sequence.store(sequence + 2, std::memory_order_release);
    }
};

// A pose from ARKit's transform: OpenXR's axes are the same (right-handed, y up, -z ahead).
void pose_from_transform(const simd_float4x4 &transform, float orientation[4], float position[3]) {
    const simd_quatf rotation = simd_quaternion(simd_matrix3x3(simd_quaternion(transform)));
    orientation[0] = rotation.vector.x;
    orientation[1] = rotation.vector.y;
    orientation[2] = rotation.vector.z;
    orientation[3] = rotation.vector.w;
    position[0] = transform.columns[3].x;
    position[1] = transform.columns[3].y;
    position[2] = transform.columns[3].z;
}

// The optics the next start renders with (read_headset_optics in openxr_runtime.cpp).
void save_optics(const float fov[2][4], float ipd, unsigned width, unsigned height) {
    const std::string path = std::string(runtime_data_directory()) + "headset_optics.txt";
    FILE *file = std::fopen(path.c_str(), "w");
    if (!file) {
        return;
    }
    std::fprintf(file, "%f %f %f %f\n%f %f %f %f\n%f\n%u %u\n", fov[0][0], fov[0][1], fov[0][2], fov[0][3], fov[1][0], fov[1][1],
                 fov[1][2], fov[1][3], ipd, width, height);
    std::fclose(file);
}

// A view's left, right, top and bottom tangents (all positive), from its projection: the
// directions its edges look along.
API_AVAILABLE(macos(26.0)) simd_float4 view_tangents(cp_drawable_t drawable, size_t index) {
    const simd_float4x4 unproject = simd_inverse(cp_drawable_compute_projection(drawable, cp_axis_direction_convention_right_up_back, index));
    const auto tangent_at = [&](float x, float y) {
        const simd_float4 point = simd_mul(unproject, simd_make_float4(x, y, 0.5f, 1.0f));
        return simd_make_float2(point.x / -point.z, point.y / -point.z);  // ahead is -z
    };
    return simd_make_float4(-tangent_at(-1.0f, 0.0f).x, tangent_at(1.0f, 0.0f).x, tangent_at(0.0f, 1.0f).y, -tangent_at(0.0f, -1.0f).y);
}

class API_AVAILABLE(macos(26.0)) Presenter {
public:
    Presenter(cp_layer_renderer_t layer, ar_device_t device) : layer_(layer), device_(device) {}

    void run() {
        gpu_ = cp_layer_renderer_get_device(layer_);
        queue_ = [gpu_ newCommandQueue];
        if (!make_pipeline() || !start_tracking()) {
            return;
        }
        runtime_log("Vision Pro: presenting");
        dispatch_async(hooks_queue(), ^{
            if (watching()) {
                g_hooks.presenting();
            }
        });
        while (!g_stop.load()) {
            switch (cp_layer_renderer_get_state(layer_)) {
                case cp_layer_renderer_state_paused:
                    // Not cp_layer_renderer_wait_until_running: stop() must be able to end this.
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                case cp_layer_renderer_state_running:
                    break;
                case cp_layer_renderer_state_invalidated:
                    runtime_log("Vision Pro: the space closed");
                    stop_tracking();
                    dispatch_async(hooks_queue(), ^{
                        if (watching()) {
                            g_hooks.ended();
                        }
                    });
                    return;
            }
            @autoreleasepool {
                present_frame();
            }
        }
        stop_tracking();
    }

private:
    bool make_pipeline() {
        NSError *error = nil;
        id<MTLLibrary> library = [gpu_ newLibraryWithSource:@(kShader) options:nil error:&error];
        if (!library) {
            runtime_logf("Vision Pro: shader: %s", error.localizedDescription.UTF8String);
            return false;
        }
        cp_layer_renderer_configuration_t configuration = cp_layer_renderer_get_configuration(layer_);
        MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = [library newFunctionWithName:@"eye_vertex"];
        descriptor.fragmentFunction = [library newFunctionWithName:@"eye_fragment"];
        descriptor.colorAttachments[0].pixelFormat = cp_layer_renderer_configuration_get_color_format(configuration);
        descriptor.depthAttachmentPixelFormat = cp_layer_renderer_configuration_get_depth_format(configuration);
        pipeline_ = [gpu_ newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (!pipeline_) {
            runtime_logf("Vision Pro: pipeline: %s", error.localizedDescription.UTF8String);
            return false;
        }
        return true;
    }

    bool start_tracking() {
        if (!device_) {
            runtime_log("Vision Pro: no device to track");
            return false;
        }
        session_ = ar_session_create_with_device(device_);
        ar_world_tracking_configuration_t configuration = ar_world_tracking_configuration_create();
        world_ = ar_world_tracking_provider_create(configuration);
        ar_data_providers_t providers = ar_data_providers_create();
        ar_data_providers_add_data_provider(providers, world_);
        ar_session_run(session_, providers);
        return true;
    }

    void stop_tracking() {
        if (session_) {
            ar_session_stop(session_);
        }
        shared_.close();
    }

    // The head at a moment (seconds, the compositor's clock), or nil while it isn't known.
    ar_device_anchor_t anchor_at(CFTimeInterval time) {
        ar_device_anchor_t anchor = ar_device_anchor_create();
        if (ar_world_tracking_provider_query_device_anchor_at_timestamp(world_, time, anchor) != ar_device_anchor_query_status_success) {
            return nil;
        }
        return anchor;
    }

    ar_device_anchor_t anchor_for(std::uint64_t key) const {
        for (const auto &entry : anchors_) {
            if (entry.anchor && entry.key == key) {
                return entry.anchor;
            }
        }
        return nil;
    }

    void present_frame() {
        cp_frame_t frame = cp_layer_renderer_query_next_frame(layer_);
        if (!frame) {
            return;
        }
        cp_frame_timing_t timing = cp_frame_predict_timing(frame);
        if (!timing) {
            return;
        }
        cp_frame_start_update(frame);
        cp_frame_end_update(frame);
        cp_time_wait_until(cp_frame_timing_get_optimal_input_time(timing));
        cp_frame_start_submission(frame);
        cp_drawable_array_t drawables = cp_frame_query_drawables(frame);
        const size_t count = drawables ? cp_drawable_array_get_count(drawables) : 0;
        if (count == 0) {
            cp_frame_end_submission(frame);
            return;
        }
        cp_drawable_t main = cp_drawable_array_get_drawable(drawables, 0);
        cp_frame_timing_t drawable_timing = cp_drawable_get_frame_timing(main);
        const CFTimeInterval presentation = cp_time_to_cf_time_interval(cp_frame_timing_get_presentation_time(drawable_timing));
        const CFTimeInterval period = presentation - previous_presentation_;
        previous_presentation_ = presentation;

        update_frames();
        publish_head(main, presentation, period > 0.0 && period < 0.1 ? period : 1.0 / 90.0);
        const bool have_frame = upload_latest();

        id<MTLCommandBuffer> commands = [queue_ commandBuffer];
        for (size_t i = 0; i < count; ++i) {
            cp_drawable_t drawable = cp_drawable_array_get_drawable(drawables, i);
            ar_device_anchor_t anchor = have_frame ? anchor_for(frame_key_) : nil;
            if (!anchor) {
                anchor = latest_anchor_;
            }
            draw(drawable, commands, have_frame);
            cp_drawable_set_device_anchor(drawable, anchor);
            cp_drawable_encode_present(drawable, commands);
        }
        [commands commit];
        cp_frame_end_submission(frame);
    }

    // Keeps the mapping of the runtime's current frames, and tells it a reader is there.
    void update_frames() {
        if (!shared_.alive()) {
            shared_.close();
            if (!shared_.open(gpu_)) {
                return;
            }
            eye_textures_[0] = eye_textures_[1] = nil;
            last_frame_id_ = 0;
        }
        shared_.header()->reader_heartbeat.fetch_add(1, std::memory_order_release);
    }

    // The head where it'll be when the game's next frame shows (a frame from now), and the
    // views' optics, for the game to draw with.
    void publish_head(cp_drawable_t drawable, CFTimeInterval presentation, CFTimeInterval period) {
        latest_anchor_ = anchor_at(presentation);
        ar_device_anchor_t ahead = anchor_at(presentation + period);
        if (!ahead || !shared_.view) {
            return;
        }
        const std::uint64_t key = static_cast<std::uint64_t>((presentation + period) * 1e9);
        anchors_[next_anchor_] = {key, ahead};
        next_anchor_ = (next_anchor_ + 1) % kAnchorHistory;

        vr_shared::HostState state{};
        state.recenter_generation = g_recenter_generation.load();
        state.tracking_timestamp_ns = key;
        pose_from_transform(ar_anchor_get_origin_from_anchor_transform(ahead), state.head_orientation, state.head_position);
        state.has_pose = 1.0f;
        const size_t views = std::min<size_t>(cp_drawable_get_view_count(drawable), 2);
        if (views == 2) {
            for (size_t i = 0; i < 2; ++i) {
                const simd_float4 tangents = view_tangents(drawable, i);
                state.fov[i][0] = -std::atan(tangents.x);
                state.fov[i][1] = std::atan(tangents.y);
                state.fov[i][2] = std::atan(tangents.z);
                state.fov[i][3] = -std::atan(tangents.w);
            }
            const simd_float4 left = cp_view_get_transform(cp_drawable_get_view(drawable, 0)).columns[3];
            const simd_float4 right = cp_view_get_transform(cp_drawable_get_view(drawable, 1)).columns[3];
            state.ipd_m = simd_length(simd_make_float3(right - left));
            state.has_views = 1;
            if (!saved_optics_) {
                id<MTLTexture> texture = cp_drawable_get_color_texture(drawable, 0);
                save_optics(state.fov, state.ipd_m, static_cast<unsigned>(texture.width), static_cast<unsigned>(texture.height));
                runtime_logf("Vision Pro: %lux%lu per eye, ipd %.1f mm", static_cast<unsigned long>(texture.width),
                             static_cast<unsigned long>(texture.height), state.ipd_m * 1000.0f);
                saved_optics_ = true;
            }
        }
        shared_.write_host_state(state);
    }

    // Copies the newest frame's eyes into textures; false while there's none.
    bool upload_latest() {
        if (!shared_.view) {
            return false;
        }
        vr_shared::SharedHeader *header = shared_.header();
        const std::int32_t latest = header->latest_slot.load(std::memory_order_acquire);
        if (latest < 0 || latest >= static_cast<std::int32_t>(vr_shared::kSlotCount)) {
            return eye_textures_[0] != nil;
        }
        const std::uint64_t slot_offset = header->first_slot_offset + static_cast<std::uint64_t>(latest) * header->slot_stride_bytes;
        auto *slot = reinterpret_cast<vr_shared::SlotHeader *>(shared_.bytes() + slot_offset);
        const std::uint32_t sequence = slot->sequence.load(std::memory_order_acquire);
        if (sequence % 2 != 0 || slot->frame_id == last_frame_id_) {
            return eye_textures_[0] != nil;
        }
        const std::uint32_t width = header->eye_width;
        const std::uint32_t height = header->eye_height;
        const MTLPixelFormat format = header->format == static_cast<std::uint32_t>(vr_shared::PixelFormat::rgba8) ? MTLPixelFormatRGBA8Unorm_sRGB : MTLPixelFormatBGRA8Unorm_sRGB;
        id<MTLTexture> textures[2];
        for (int eye = 0; eye < 2; ++eye) {
            MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
            descriptor.usage = MTLTextureUsageShaderRead;
            descriptor.storageMode = MTLStorageModePrivate;
            id<MTLTexture> spare = spare_textures_[eye];
            textures[eye] = spare && spare.width == width && spare.height == height && spare.pixelFormat == format ? spare : [gpu_ newTextureWithDescriptor:descriptor];
        }
        id<MTLCommandBuffer> commands = [queue_ commandBuffer];
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        for (int eye = 0; eye < 2; ++eye) {
            [blit copyFromBuffer:shared_.buffer
                       sourceOffset:slot_offset + sizeof(vr_shared::SlotHeader) + static_cast<std::uint64_t>(eye) * header->eye_size_bytes
                  sourceBytesPerRow:header->eye_stride_bytes
                sourceBytesPerImage:header->eye_size_bytes
                         sourceSize:MTLSizeMake(width, height, 1)
                          toTexture:textures[eye]
                   destinationSlice:0
                   destinationLevel:0
                  destinationOrigin:MTLOriginMake(0, 0, 0)];
        }
        [blit endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        // The runtime started writing this slot again while it was copied: keep the last frame.
        std::atomic_thread_fence(std::memory_order_acquire);
        if (slot->sequence.load(std::memory_order_acquire) != sequence) {
            spare_textures_[0] = textures[0];
            spare_textures_[1] = textures[1];
            return eye_textures_[0] != nil;
        }
        for (int eye = 0; eye < 2; ++eye) {
            spare_textures_[eye] = eye_textures_[eye];
            eye_textures_[eye] = textures[eye];
            const vr_shared::ViewPose &view = slot->views[eye];
            eye_tangents_[eye] = simd_make_float4(std::tan(view.fov_left), std::tan(view.fov_right), std::tan(view.fov_up), std::tan(view.fov_down));
        }
        last_frame_id_ = slot->frame_id;
        frame_key_ = slot->tracking_timestamp_ns;
        return true;
    }

    void draw(cp_drawable_t drawable, id<MTLCommandBuffer> commands, bool have_frame) {
        const size_t views = cp_drawable_get_view_count(drawable);
        for (size_t i = 0; i < views; ++i) {
            cp_view_t view = cp_drawable_get_view(drawable, i);
            cp_view_texture_map_t map = cp_view_get_view_texture_map(view);
            const size_t index = cp_view_texture_map_get_texture_index(map);
            MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = cp_drawable_get_color_texture(drawable, index);
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            // Everything far away: the Vision Pro corrects the picture for the head's turn only.
            pass.depthAttachment.texture = cp_drawable_get_depth_texture(drawable, index);
            pass.depthAttachment.loadAction = MTLLoadActionClear;
            pass.depthAttachment.clearDepth = 0.0;
            pass.depthAttachment.storeAction = MTLStoreActionStore;
            id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
            const size_t eye = std::min<size_t>(i, 1);
            if (have_frame && eye_textures_[eye]) {
                [encoder setViewport:cp_view_texture_map_get_viewport(map)];
                [encoder setRenderPipelineState:pipeline_];
                Mapping mapping{view_tangents(drawable, i), eye_tangents_[eye]};
                [encoder setVertexBytes:&mapping length:sizeof(mapping) atIndex:0];
                [encoder setFragmentBytes:&mapping length:sizeof(mapping) atIndex:0];
                [encoder setFragmentTexture:eye_textures_[eye] atIndex:0];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            }
            [encoder endEncoding];
        }
    }

    struct Anchor {
        std::uint64_t key = 0;
        ar_device_anchor_t anchor = nil;
    };

    cp_layer_renderer_t layer_;
    ar_device_t device_;
    id<MTLDevice> gpu_ = nil;
    id<MTLCommandQueue> queue_ = nil;
    id<MTLRenderPipelineState> pipeline_ = nil;
    ar_session_t session_ = nil;
    ar_world_tracking_provider_t world_ = nil;
    Shared shared_;
    Anchor anchors_[kAnchorHistory];
    size_t next_anchor_ = 0;
    ar_device_anchor_t latest_anchor_ = nil;
    CFTimeInterval previous_presentation_ = 0.0;
    id<MTLTexture> eye_textures_[2] = {nil, nil};
    id<MTLTexture> spare_textures_[2] = {nil, nil};
    simd_float4 eye_tangents_[2] = {};
    std::uint32_t last_frame_id_ = 0;
    std::uint64_t frame_key_ = 0;
    bool saved_optics_ = false;
};

}  // namespace

extern "C" void purr_vision_pro_open(void);
extern "C" void purr_vision_pro_close(void);

namespace {

// Asks for the space (macOS asks which Vision Pro; the player accepts on the headset), not while
// another headset has the game.
void ask_for_space() {
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_watching || g_asked) {
            return;
        }
    }
    if (!g_hooks.may_open()) {
        runtime_log("Vision Pro: one is nearby, but another headset has the game");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_watching || g_asked) {
            return;
        }
        g_asked = true;
    }
    runtime_log("Vision Pro: one is nearby, opening the space");
    dispatch_async(dispatch_get_main_queue(), ^{
        purr_vision_pro_open();
    });
}

}  // namespace

// Only ever called on macOS 26 or later, by the scene.
extern "C" API_AVAILABLE(macos(26.0)) void purr_vision_pro_layer_ready(void *layer_renderer, void *device) {
    cp_layer_renderer_t layer = (__bridge_transfer cp_layer_renderer_t)layer_renderer;
    ar_device_t ar_device = device ? (__bridge_transfer ar_device_t)device : nil;
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_watching) {
        return;  // VR stopped while the player was choosing
    }
    if (g_thread.joinable()) {  // a space opened again: the last one has closed
        g_stop = true;
        g_thread.join();
    }
    g_stop = false;
    g_thread = std::thread([layer, ar_device] {
        Presenter presenter(layer, ar_device);
        presenter.run();
    });
}

extern "C" void purr_vision_pro_failed(const char *reason) {
    runtime_logf("Vision Pro: not shown: %s", reason);
}

extern "C" void purr_vision_pro_log(const char *message) {
    runtime_log(message);
}

extern "C" void purr_vision_pro_recentred(void) {
    g_recenter_generation.fetch_add(1);
}

namespace vision_pro {

void start(Hooks hooks) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_watching) {
        return;
    }
    g_watching = true;
    g_asked = false;
    g_hooks = std::move(hooks);
}

void nearby(bool present) {
    if (!present) {
        std::lock_guard<std::mutex> lock(g_lock);
        g_asked = false;
        return;
    }
    dispatch_async(hooks_queue(), ^{
        ask_for_space();
    });
}

void stop() {
    bool asked = false;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (!g_watching) {
            return;
        }
        g_watching = false;
        g_stop = true;
        asked = g_asked || g_thread.joinable();
        if (g_thread.joinable()) {
            g_thread.join();
        }
        g_asked = false;
    }
    if (asked) {
        dispatch_async(dispatch_get_main_queue(), ^{
            purr_vision_pro_close();
        });
    }
}

}  // namespace vision_pro
