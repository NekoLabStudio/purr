#pragma once

// Layout of the shared frame buffer between the OpenXR runtime (in the
// game's process) and the streamer (its own process, streamer/src/
// shared_frames.rs, which addresses these fields by byte offset).
//
// The runtime maps a file in the user's temporary folder and the GPU copies
// each frame's eyes straight into it; the streamer maps the same file. No
// data goes to disk in the steady state: the pages are re-dirtied far faster
// than the kernel flushes them.
//
// One writer (the runtime, from the GPU's completion), one reader (the
// streamer). Slots rotate; each slot is protected by a seqlock so the reader
// never sees a torn frame.

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace vr_shared {

constexpr std::uint32_t kMagic = 0x52565748;  // "HWVR"
constexpr std::uint32_t kVersion = 4;

// Three slots: the writer fills one while the reader may still be copying the
// previous one, with a spare so the two never collide.
constexpr std::uint32_t kSlotCount = 3;

// Guards against a corrupt or hostile header steering the reader out of bounds.
constexpr std::uint32_t kMaxEyeWidth = 8192;
constexpr std::uint32_t kMaxEyeHeight = 8192;

// The runtime stops copying frames out when the streamer has not bumped
// reader_heartbeat for this long, so the game pays nothing when no headset
// is watching.
constexpr std::uint64_t kReaderTimeoutNs = 2ULL * 1000000000ULL;

enum class PixelFormat : std::uint32_t {
    unknown = 0,
    rgba8 = 1,  // R8G8B8A8, sRGB-encoded
    bgra8 = 2,  // B8G8R8A8, sRGB-encoded
};

#pragma pack(push, 8)

// Pose the frame was rendered with, carried alongside the pixels. Mirrors
// XrPosef/XrFovf.
struct ViewPose {
    float orientation_x = 0.0f;
    float orientation_y = 0.0f;
    float orientation_z = 0.0f;
    float orientation_w = 1.0f;
    float position_x = 0.0f;
    float position_y = 0.0f;
    float position_z = 0.0f;
    float fov_left = 0.0f;
    float fov_right = 0.0f;
    float fov_up = 0.0f;
    float fov_down = 0.0f;
    float reserved = 0.0f;
};

struct SlotHeader {
    // Even = stable, odd = a write is in progress. The reader samples this
    // before and after copying and retries if it changed.
    std::atomic<std::uint32_t> sequence;
    std::uint32_t frame_id;

    // OpenXR time (the runtime's clock, nanoseconds).
    std::uint64_t predicted_display_time_ns;
    // Wall clock, nanoseconds since the Unix epoch, comparable between the
    // two processes.
    std::uint64_t submitted_at_unix_ns;   // entry to xrEndFrame for this frame
    std::uint64_t published_at_unix_ns;   // pixels finished copying into this slot

    // How long the GPU readback and the copy into shared memory took.
    std::uint32_t readback_us;
    std::uint32_t copy_us;

    ViewPose views[2];

    // The HostState::tracking_timestamp_ns the frame's head pose came from.
    // The streamer stamps the encoded frame with it so the headset reprojects
    // from the pose it actually rendered with.
    std::uint64_t tracking_timestamp_ns;
};

// Written by the streamer, read by the runtime: the latest head pose from
// the headset and the optics to render with. Lives in the header page at
// kHostStateOffset, seqlocked like a slot.
struct HostState {
    std::atomic<std::uint32_t> sequence;
    // Bumped every time the headset recentres. The runtime moves its LOCAL
    // space origin to the current head pose when it changes.
    std::uint32_t recenter_generation;

    // The tracking timestamp of this pose (the headset's clock).
    std::uint64_t tracking_timestamp_ns;
    std::uint64_t written_at_unix_ns;

    // Head pose in the headset's world: metres, floor origin, yaw zeroed at recentre.
    float head_orientation[4];  // x y z w
    float head_position[3];
    float has_pose;             // 0 or 1, kept as float for alignment
    float linear_velocity[3];
    float reserved0;
    float angular_velocity[3];
    float reserved1;

    // Per-eye field of view (left, right, up, down; radians) and IPD from the
    // headset, valid when has_views is non-zero.
    float fov[2][4];
    float ipd_m;
    std::uint32_t has_views;
};

// A controller's inputs, as values: buttons and touches are 0 or 1, triggers
// and the squeeze 0 to 1, the thumbstick -1 to 1. The paths are OpenXR's
// Meta Touch controller's, onto which every headset's controllers are mapped.
enum Input : std::uint32_t {
    kInputMenuClick,        // left
    kInputSystemClick,      // right; the system reserves it
    kInputAClick,           // right
    kInputATouch,
    kInputBClick,
    kInputBTouch,
    kInputXClick,           // left
    kInputXTouch,
    kInputYClick,
    kInputYTouch,
    kInputSqueezeValue,
    kInputTriggerValue,
    kInputTriggerTouch,
    kInputThumbstickX,
    kInputThumbstickY,
    kInputThumbstickClick,
    kInputThumbstickTouch,
    kInputThumbrestTouch,
    kInputCount,
};
constexpr std::uint32_t kInputSlots = 20;  // room for more without moving anything

// One hand's controller: its grip pose (OpenXR's grip) in the headset's world, like
// the head's, and its inputs.
struct ControllerState {
    std::uint32_t active;  // tracked and connected
    std::uint32_t reserved0;
    float orientation[4];  // x y z w
    float position[3];
    float reserved1;
    float linear_velocity[3];
    float reserved2;
    float angular_velocity[3];
    float reserved3;
    float inputs[kInputSlots];  // by Input
};

// Written by the streamer, read by the runtime: both hands, at
// kInputStateOffset, seqlocked like the host state.
struct InputState {
    std::atomic<std::uint32_t> sequence;
    std::uint32_t reserved;
    ControllerState hands[2];  // left, right
};

// Written by the runtime, read by the streamer: the latest vibration asked
// for on each hand, at kHapticStateOffset. A new request bumps its serial.
struct HapticPulse {
    std::uint64_t serial;
    float duration_s;  // 0: the shortest the controller does
    float frequency;   // Hz, 0: the controller's own
    float amplitude;   // 0 to 1; 0 stops a vibration
    float reserved;
};

struct HapticState {
    std::atomic<std::uint32_t> sequence;
    std::uint32_t reserved;
    HapticPulse hands[2];
};

struct SharedHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t slot_count;
    std::uint32_t format;  // PixelFormat

    std::uint32_t eye_width;
    std::uint32_t eye_height;
    std::uint32_t eye_stride_bytes;
    std::uint32_t eye_size_bytes;

    std::uint64_t slot_stride_bytes;  // distance between consecutive slots
    std::uint64_t first_slot_offset;  // byte offset of slot 0 from the mapping base
    std::uint64_t total_size_bytes;

    // Index of the most recently published slot, or -1 before the first frame.
    std::atomic<std::int32_t> latest_slot;
    std::uint32_t reserved0;

    std::atomic<std::uint64_t> frames_published;
    std::atomic<std::uint64_t> frames_dropped;  // readback not ready in time

    // The reader bumps this every time it looks; the runtime skips the copy
    // entirely once it stops changing.
    std::atomic<std::uint64_t> reader_heartbeat;

    // Bumped on every xrEndFrame whether or not anything is copied, so the
    // streamer can see the game is running without causing any work.
    std::atomic<std::uint64_t> frames_submitted;
};

#pragma pack(pop)

static_assert(sizeof(ViewPose) == 48, "Unexpected ViewPose size");
static_assert(sizeof(SlotHeader) == 144, "Unexpected SlotHeader size");
static_assert(sizeof(SharedHeader) == 96, "Unexpected SharedHeader size");
static_assert(sizeof(HostState) == 128, "Unexpected HostState size");
static_assert(sizeof(ControllerState) == 152, "Unexpected ControllerState size");
static_assert(sizeof(InputState) == 312, "Unexpected InputState size");
static_assert(sizeof(HapticState) == 56, "Unexpected HapticState size");
static_assert(offsetof(ControllerState, orientation) == 8, "ControllerState layout changed");
static_assert(offsetof(ControllerState, position) == 24, "ControllerState layout changed");
static_assert(offsetof(ControllerState, linear_velocity) == 40, "ControllerState layout changed");
static_assert(offsetof(ControllerState, angular_velocity) == 56, "ControllerState layout changed");
static_assert(offsetof(ControllerState, inputs) == 72, "ControllerState layout changed");
static_assert(offsetof(InputState, hands) == 8, "InputState layout changed");
static_assert(offsetof(HapticState, hands) == 8, "HapticState layout changed");

// The streamer (streamer/src/shared_frames.rs) addresses these fields by
// byte offset.
static_assert(offsetof(SharedHeader, latest_slot) == 56, "SharedHeader layout changed");
static_assert(offsetof(SharedHeader, frames_dropped) == 72, "SharedHeader layout changed");
static_assert(offsetof(SharedHeader, reader_heartbeat) == 80, "SharedHeader layout changed");
static_assert(offsetof(SharedHeader, frames_submitted) == 88, "SharedHeader layout changed");
static_assert(offsetof(SlotHeader, submitted_at_unix_ns) == 16, "SlotHeader layout changed");
static_assert(offsetof(SlotHeader, tracking_timestamp_ns) == 136, "SlotHeader layout changed");
static_assert(offsetof(HostState, tracking_timestamp_ns) == 8, "HostState layout changed");
static_assert(offsetof(HostState, head_orientation) == 24, "HostState layout changed");
static_assert(offsetof(HostState, head_position) == 40, "HostState layout changed");
static_assert(offsetof(HostState, has_pose) == 52, "HostState layout changed");
static_assert(offsetof(HostState, linear_velocity) == 56, "HostState layout changed");
static_assert(offsetof(HostState, angular_velocity) == 72, "HostState layout changed");
static_assert(offsetof(HostState, fov) == 88, "HostState layout changed");
static_assert(offsetof(HostState, ipd_m) == 120, "HostState layout changed");
static_assert(offsetof(HostState, has_views) == 124, "HostState layout changed");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "Need lock-free 32-bit atomics");
static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "Need lock-free 64-bit atomics");

// Slots start on a page boundary so the two processes never share a partially
// written page between the header and the first slot. The host-written state
// sits in the same page, after the header, so the whole file has two writers
// but no shared cache line.
constexpr std::uint64_t kHostStateOffset = 256;
// The streamer writes the controllers here, and the runtime the haptics apart
// from it, so the two writers never share a cache line.
constexpr std::uint64_t kInputStateOffset = 512;
constexpr std::uint64_t kHapticStateOffset = 1024;
constexpr std::uint64_t kFirstSlotOffset = 4096;

// Pixel rows are copied tightly packed, so the stride is just the row size.
constexpr std::uint64_t slot_stride(std::uint32_t eye_size_bytes) {
    return sizeof(SlotHeader) + 2ULL * eye_size_bytes;
}

constexpr std::uint64_t total_size(std::uint32_t eye_size_bytes) {
    return kFirstSlotOffset + kSlotCount * slot_stride(eye_size_bytes);
}

}  // namespace vr_shared
