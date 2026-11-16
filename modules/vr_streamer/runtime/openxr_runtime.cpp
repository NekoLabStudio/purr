// Purr's OpenXR runtime on macOS, where no headset maker ships one.
//
// The vr_streamer module points Godot's OpenXR loader at this library, and
// the game renders through it as through any runtime: one stereo view
// configuration, LOCAL/STAGE/VIEW reference spaces, Metal swapchains
// (XR_KHR_metal_enable) on the game's own device, and a frame loop paced to
// the headset. Each frame's eyes go to the streamer (streamer_process.cpp)
// through shared memory (frame_export.mm); the headset's pose comes back
// the same way, and so do the controllers, as the Meta Touch controller
// (Input, below).

#include "graphics_vulkan.hpp"
#ifdef _WIN32
#include "graphics_d3d11.hpp"
#include "graphics_d3d12.hpp"
#endif

#include "graphics.hpp"
#include "latency_critical.hpp"
#include "platform.hpp"
#include "runtime_log.hpp"
#include "shared_frames.hpp"
#include "streamer_process.hpp"
#ifdef __APPLE__
#include "vision_pro.hpp"
#endif
#include "xr_math.hpp"

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>
#include <openxr/openxr_reflection.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr char kRuntimeName[] = "VR streamer";
constexpr XrVersion kRuntimeVersion = XR_MAKE_VERSION(0, 1, 0);
constexpr char kSystemName[] = "Headset over the network";

// Per-eye size and frame rate, which the streamer is told so it negotiates
// the same with the headset (sizes are multiples of 32): frames reach the encoder without scaling. The headset's display
// scales the picture up to its panels. The player's choices (the game's
// settings, in the streamer's stream.json: eye_scale, a share of the full
// size, and hz) set them at each start; VR_EYE_SIZE=WxH and VR_HZ override
// them, for measuring.
constexpr uint32_t kFullViewWidth = 2080;  // about a Quest 3's panel per eye
constexpr uint32_t kFullViewHeight = 2240;
// Each eye's full size: a Quest 3's panel until the streamer has saved the connected headset's
// own (headset_optics.txt), and what the game renders unless the player chooses less.
uint32_t g_full_width = kFullViewWidth;
uint32_t g_full_height = kFullViewHeight;
uint32_t g_view_width = kFullViewWidth;
uint32_t g_view_height = kFullViewHeight;
constexpr uint32_t kMaxViewSize = 8192;
double g_display_hz = 72.0;
double g_chosen_hz = 72.0;  // the player's, for Steam Link
constexpr double kVisionProHz = 90.0;
XrDuration g_frame_period_ns = static_cast<XrDuration>(1000000000.0 / 72.0);

bool supported_hz(double hz) {
    return hz == 72.0 || hz == 80.0 || hz == 90.0 || hz == 120.0;
}

// A share of the full size, on a grid of 32.
uint32_t scaled_view(uint32_t full, double share) {
    return static_cast<uint32_t>(std::lround(full * share / 32.0)) * 32;
}

// A number from the choices file's flat JSON object, if it has one under this key.
bool json_number(const std::string& text, const char* key, double& out) {
    const std::string quoted = std::string("\"") + key + "\"";
    const size_t at = text.find(quoted);
    if (at == std::string::npos) {
        return false;
    }
    const size_t colon = text.find(':', at + quoted.size());
    if (colon == std::string::npos) {
        return false;
    }
    char* end = nullptr;
    const char* start = text.c_str() + colon + 1;
    out = std::strtod(start, &end);
    return end != start;
}

void read_player_choices() {
    std::ifstream file(std::string(runtime_data_directory()) + "stream.json");
    if (!file) {
        return;
    }
    std::stringstream contents;
    contents << file.rdbuf();
    const std::string text = contents.str();
    double scale = 0.0;
    if (json_number(text, "eye_scale", scale) && scale >= 0.5 && scale <= 1.0) {
        g_view_width = scaled_view(g_full_width, scale);
        g_view_height = scaled_view(g_full_height, scale);
    }
    double hz = 0.0;
    if (json_number(text, "hz", hz) && supported_hz(hz)) {
        g_display_hz = hz;
    }
}

void read_overrides() {
    unsigned width = 0;
    unsigned height = 0;
    const char* size = std::getenv("VR_EYE_SIZE");
    if (size && std::sscanf(size, "%ux%u", &width, &height) == 2 && width >= 256 && height >= 256 &&
        width <= kMaxViewSize && height <= kMaxViewSize) {
        g_view_width = width / 32 * 32;
        g_view_height = height / 32 * 32;
    }
    const char* hz = std::getenv("VR_HZ");
    if (hz && supported_hz(std::atof(hz))) {
        g_display_hz = std::atof(hz);
    }
    g_frame_period_ns = static_cast<XrDuration>(1000000000.0 / g_display_hz);
}

// Quest 3 optics as the headset reports them, used until the streamer has
// saved the connected headset's own (headset_optics.txt). The game reads the
// FOV once and we declare it immutable, so it should be right before the
// headset connects: a frame drawn wider than the headset shows wastes pixels,
// and one drawn narrower leaves the edges empty. Left eye; the right mirrors
// it.
constexpr XrFovf kLeftEyeFov{-0.94247776f, 0.6981317f, 0.7679449f, -0.9599311f};
constexpr float kInterpupillaryDistance = 0.0631f;

// Before tracking arrives the head sits this high over STAGE's floor.
constexpr float kSeatedEyeHeight = 1.2f;

constexpr uint32_t kMaxSwapchains = 16;
constexpr uint32_t kMaxSpaces = 64;
constexpr uint32_t kMaxActionSets = 16;
constexpr uint32_t kMaxActions = 256;
constexpr uint32_t kMaxQueuedEvents = 32;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct Instance {
    bool alive = false;
    bool refresh_rate_events = false;  // XR_FB_display_refresh_rate on
};

struct Space {
    bool alive = false;
    bool is_action_space = false;
    XrReferenceSpaceType reference_type = XR_REFERENCE_SPACE_TYPE_LOCAL;
    XrPosef pose_in_reference = pose_identity();
    // An action space follows a hand's controller: the hand its action and
    // subaction path resolve to (-1: none).
    XrAction action = XR_NULL_HANDLE;
    XrPath subaction_path = XR_NULL_PATH;
};

struct ActionSet {
    bool alive = false;
};

struct Action {
    bool alive = false;
    XrActionSet set = XR_NULL_HANDLE;
    XrActionType type = XR_ACTION_TYPE_BOOLEAN_INPUT;
    std::vector<XrPath> subaction_paths;
    // The bindings the game suggested, by interaction profile.
    std::vector<XrPath> touch_bindings;   // /interaction_profiles/oculus/touch_controller
    std::vector<XrPath> simple_bindings;  // /interaction_profiles/khr/simple_controller
    // Boolean state at the last sync, for each hand, and when it changed.
    bool last_bool[2] = {false, false};
    float last_float[2] = {0.0f, 0.0f};
    XrVector2f last_vector[2] = {{0.0f, 0.0f}, {0.0f, 0.0f}};
    XrTime last_change[2] = {0, 0};
};

struct Swapchain {
    bool alive = false;
    XrSwapchainCreateInfo info{};
    void* images[kSwapchainImageCount] = {};  // the graphics backend's
    // Index handed out by xrAcquireSwapchainImage and not yet released, or -1.
    int acquired = -1;
    // Index the app most recently released: what xrEndFrame sends.
    int last_released = -1;
    uint32_t next = 0;
};

struct OpaqueHandle {
    bool alive = false;
};

struct Session {
    bool alive = false;
    XrSessionState state = XR_SESSION_STATE_UNKNOWN;
    bool running = false;
    XrTime last_predicted_display = 0;
    bool frame_begun = false;
    // The frame copies couldn't be set up (no memory to read frames back into, say): not tried
    // again each frame, which would allocate again each time.
    bool export_failed = false;
    uint64_t frame_count = 0;
};

Instance g_instance;
Session g_session;
Space g_spaces[kMaxSpaces];
Swapchain g_swapchains[kMaxSwapchains];
ActionSet g_action_sets[kMaxActionSets];
Action g_actions[kMaxActions];
std::vector<std::string> g_paths;  // XrPath n is g_paths[n - 1]
XrEventDataBuffer g_events[kMaxQueuedEvents];
uint32_t g_event_head = 0;
uint32_t g_event_count = 0;

// The session's graphics (graphics.hpp), made from its binding.
std::unique_ptr<Graphics> g_graphics;

// Head pose in the world (STAGE) frame, from the headset through the
// streamer. Until the first pose arrives the head sits at the seated
// height, looking ahead.
XrPosef g_head_pose = pose_identity();
bool g_have_tracking = false;
// Where LOCAL's origin sits in the world: the head when tracking first
// arrived, moved whenever the headset recentres.
XrPosef g_local_origin = pose_identity();
uint32_t g_recenter_generation = 0;
// The headset's own field of view, and the one the game draws (with the margin, add_margin()).
XrFovf g_headset_fov[2] = {kLeftEyeFov, {-kLeftEyeFov.angleRight, -kLeftEyeFov.angleLeft, kLeftEyeFov.angleUp, kLeftEyeFov.angleDown}};
XrFovf g_fov[2] = {g_headset_fov[0], g_headset_fov[1]};

// A margin drawn beyond the headset's field of view, in each direction, as a share of the view's
// tangents: when the head turns faster than the next frame arrives, the headset turns the last
// frame to match and shows this margin rather than black. The eye's size grows with it, so the
// picture keeps its sharpness.
constexpr double kMargin = 1.08;

void add_margin() {
    const auto wider = [](float angle) { return static_cast<float>(std::atan(std::tan(angle) * kMargin)); };
    for (int eye = 0; eye < 2; ++eye) {
        const XrFovf& fov = g_headset_fov[eye];
        g_fov[eye] = XrFovf{wider(fov.angleLeft), wider(fov.angleRight), wider(fov.angleUp), wider(fov.angleDown)};
    }
    const auto grown = [](uint32_t size) {
        return std::min<uint32_t>(static_cast<uint32_t>(std::lround(size * kMargin / 32.0)) * 32, kMaxViewSize);
    };
    g_view_width = grown(g_view_width);
    g_view_height = grown(g_view_height);
}
float g_ipd = kInterpupillaryDistance;
// The tracking sample each xrLocateViews answered with, by the display time
// asked about, so a frame is sent stamped with the pose it was rendered from.
struct LocatedFrame {
    XrTime display_time = 0;
    uint64_t tracking_timestamp_ns = 0;
    XrPosef head = pose_identity();
};
LocatedFrame g_located[8];
uint32_t g_located_next = 0;
uint64_t g_last_tracking_timestamp_ns = 0;

SharedFrames g_frames;

// The frame xrEndFrame handed over, copied out at the next xrWaitFrame:
// Godot commits a frame's GPU work after xrEndFrame returns, and a copy
// queued before that commit reads a half-drawn picture.
struct PendingFrame {
    bool waiting = false;
    Graphics::EyeSource eyes[2];
    SharedFrames::Frame frame;
};
PendingFrame g_pending;

void send_pending_frame() {
    if (!g_pending.waiting || !g_graphics) {
        return;
    }
    g_pending.waiting = false;
    g_graphics->export_frame(g_frames, g_pending.eyes, g_pending.frame);
}
std::recursive_mutex g_lock;
using ScopedLock = std::lock_guard<std::recursive_mutex>;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

XrTime now_ns() {
    return static_cast<XrTime>(platform::monotonic_ns());
}

void sleep_until(XrTime target) {
    platform::sleep_until(target > 0 ? static_cast<std::uint64_t>(target) : 0);
}

const char* result_name(XrResult result);
const char* structure_name(XrStructureType type);

// The controllers (Input, below).
bool check_action(XrAction action);
bool controller_pose(int hand, XrPosef& pose, XrVector3f* linear, XrVector3f* angular);
int action_space_hand(const Space& space);

void queue_session_state(XrSessionState state) {
    if (g_event_count == kMaxQueuedEvents) {
        runtime_log("event queue full, dropping a session state");
        return;
    }
    XrEventDataBuffer& slot = g_events[(g_event_head + g_event_count) % kMaxQueuedEvents];
    std::memset(&slot, 0, sizeof(slot));
    auto* event = reinterpret_cast<XrEventDataSessionStateChanged*>(&slot);
    event->type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
    event->session = reinterpret_cast<XrSession>(&g_session);
    event->state = state;
    event->time = now_ns();
    ++g_event_count;
    g_session.state = state;
}

// The rate the game draws at now: the player's choice, or a Vision Pro's while one shows the
// game. Called with the lock held.
void set_display_rate(double hz) {
    if (hz == g_display_hz) {
        return;
    }
    runtime_logf("drawing at %.0f Hz", hz);
    const double before = g_display_hz;
    g_display_hz = hz;
    g_frame_period_ns = static_cast<XrDuration>(1000000000.0 / hz);
    if (!g_instance.refresh_rate_events || !g_session.alive || g_event_count == kMaxQueuedEvents) {
        return;
    }
    XrEventDataBuffer& slot = g_events[(g_event_head + g_event_count) % kMaxQueuedEvents];
    std::memset(&slot, 0, sizeof(slot));
    auto* event = reinterpret_cast<XrEventDataDisplayRefreshRateChangedFB*>(&slot);
    event->type = XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB;
    event->fromDisplayRefreshRate = static_cast<float>(before);
    event->toDisplayRefreshRate = static_cast<float>(hz);
    ++g_event_count;
}

void start_streamer() {
    streamer_process::start(SharedFrames::shared_path(), g_view_width, g_view_height, g_display_hz);
}

void queue_reference_space_change(XrReferenceSpaceType type) {
    if (g_event_count == kMaxQueuedEvents || !g_session.alive) {
        return;
    }
    XrEventDataBuffer& slot = g_events[(g_event_head + g_event_count) % kMaxQueuedEvents];
    std::memset(&slot, 0, sizeof(slot));
    auto* event = reinterpret_cast<XrEventDataReferenceSpaceChangePending*>(&slot);
    event->type = XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING;
    event->session = reinterpret_cast<XrSession>(&g_session);
    event->referenceSpaceType = type;
    event->changeTime = now_ns();
    event->poseValid = XR_FALSE;
    ++g_event_count;
}

// The streamer saves the headset's optics whenever one connects
// (headset_optics.txt: each eye's FOV, left right up down in radians, then
// the IPD in metres, then, when it knows the headset, each eye's full size),
// for the next start to render with.
void read_headset_optics() {
    const std::string path = std::string(runtime_data_directory()) + "headset_optics.txt";
    FILE* file = std::fopen(path.c_str(), "r");
    if (!file) {
        return;
    }
    XrFovf fov[2];
    float ipd = 0.0f;
    unsigned width = 0;
    unsigned height = 0;
    const int read = std::fscanf(file, "%f %f %f %f\n%f %f %f %f\n%f\n%u %u", &fov[0].angleLeft, &fov[0].angleRight,
                                 &fov[0].angleUp, &fov[0].angleDown, &fov[1].angleLeft, &fov[1].angleRight,
                                 &fov[1].angleUp, &fov[1].angleDown, &ipd, &width, &height);
    std::fclose(file);
    if (read == 11 && width >= 256 && height >= 256 && width <= kMaxViewSize && height <= kMaxViewSize) {
        g_full_width = g_view_width = width / 32 * 32;
        g_full_height = g_view_height = height / 32 * 32;
    }
    if (read < 9 || ipd < 0.04f || ipd > 0.09f) {
        runtime_logf("ignoring malformed %s", path.c_str());
        return;
    }
    g_headset_fov[0] = fov[0];
    g_headset_fov[1] = fov[1];
    g_ipd = ipd;
}

// Takes the newest head pose and optics from the streamer. Called with the
// lock held, from the frame loop.
void refresh_tracking() {
    vr_shared::HostState state{};
    if (!g_frames.read_host_state(state)) {
        return;
    }
    if (state.has_views && state.ipd_m > 0.04f && state.ipd_m < 0.09f) {
        g_ipd = state.ipd_m;
    }
    if (state.has_pose == 0.0f) {
        return;
    }
    g_head_pose.orientation = XrQuaternionf{state.head_orientation[0], state.head_orientation[1],
                                            state.head_orientation[2], state.head_orientation[3]};
    g_head_pose.position = XrVector3f{state.head_position[0], state.head_position[1], state.head_position[2]};

    // LOCAL's origin sits at the head, facing where it faces (yaw only, so
    // the horizon stays level): the game's ahead is where the player looks.
    if (!g_have_tracking || state.recenter_generation != g_recenter_generation) {
        XrQuaternionf yaw = g_head_pose.orientation;
        yaw.x = 0.0f;
        yaw.z = 0.0f;
        const float length = std::sqrt(yaw.y * yaw.y + yaw.w * yaw.w);
        if (length > 0.0001f) {
            yaw.y /= length;
            yaw.w /= length;
        } else {
            yaw = quat_identity();
        }
        g_local_origin = XrPosef{yaw, g_head_pose.position};
        if (g_have_tracking) {
            queue_reference_space_change(XR_REFERENCE_SPACE_TYPE_LOCAL);  // the game recentres with it
        }
        g_recenter_generation = state.recenter_generation;
    }
    g_have_tracking = true;
    g_last_tracking_timestamp_ns = state.tracking_timestamp_ns;
}

// A space's origin in the world frame (STAGE: the headset's floor origin).
XrPosef space_origin_in_world(const Space& space) {
    XrPosef local_origin = g_local_origin;
    XrPosef head = g_head_pose;
    if (!g_have_tracking) {
        local_origin.position.y = kSeatedEyeHeight;
        head = pose_multiply(local_origin, g_head_pose);
    }
    XrPosef reference = pose_identity();
    switch (space.reference_type) {
    case XR_REFERENCE_SPACE_TYPE_STAGE:
        break;
    case XR_REFERENCE_SPACE_TYPE_LOCAL:
        reference = local_origin;
        break;
    case XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR:
        reference = local_origin;
        reference.position.y = 0.0f;
        break;
    case XR_REFERENCE_SPACE_TYPE_VIEW:
        reference = head;
        break;
    default:
        break;
    }
    return pose_multiply(reference, space.pose_in_reference);
}

template <typename T, uint32_t N>
T* allocate_slot(T (&pool)[N]) {
    for (uint32_t i = 0; i < N; ++i) {
        if (!pool[i].alive) {
            pool[i] = T{};
            pool[i].alive = true;
            return &pool[i];
        }
    }
    return nullptr;
}

template <typename T, uint32_t N>
bool is_live_handle(T (&pool)[N], const void* handle) {
    const auto* candidate = static_cast<const T*>(handle);
    return candidate >= &pool[0] && candidate < &pool[N] && candidate->alive;
}

bool check_instance(XrInstance instance) {
    return reinterpret_cast<Instance*>(instance) == &g_instance && g_instance.alive;
}

bool check_session(XrSession session) {
    return reinterpret_cast<Session*>(session) == &g_session && g_session.alive;
}

void copy_string(char* destination, size_t destination_size, const char* source) {
    std::snprintf(destination, destination_size, "%s", source ? source : "");
}

// The two-call idiom: report the count, then copy when there's room.
template <typename T>
XrResult return_array(uint32_t capacity, uint32_t* count_output, T* out, const T* values, uint32_t count) {
    if (!count_output) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    *count_output = count;
    if (capacity == 0) {
        return XR_SUCCESS;
    }
    if (capacity < count || !out) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }
    for (uint32_t i = 0; i < count; ++i) {
        out[i] = values[i];
    }
    return XR_SUCCESS;
}

// ---------------------------------------------------------------------------
// Instance
// ---------------------------------------------------------------------------

// Metal on macOS, Vulkan everywhere (MoltenVK on macOS) and Direct3D 11 and 12 on Windows.
const XrExtensionProperties kSupportedExtensions[] = {
#ifdef __APPLE__
    {XR_TYPE_EXTENSION_PROPERTIES, nullptr, XR_KHR_METAL_ENABLE_EXTENSION_NAME, XR_KHR_metal_enable_SPEC_VERSION},
#endif
    {XR_TYPE_EXTENSION_PROPERTIES, nullptr, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME, XR_KHR_vulkan_enable2_SPEC_VERSION},
#ifdef _WIN32
    {XR_TYPE_EXTENSION_PROPERTIES, nullptr, XR_KHR_D3D11_ENABLE_EXTENSION_NAME, XR_KHR_D3D11_enable_SPEC_VERSION},
    {XR_TYPE_EXTENSION_PROPERTIES, nullptr, XR_KHR_D3D12_ENABLE_EXTENSION_NAME, XR_KHR_D3D12_enable_SPEC_VERSION},
#endif
    // The headset's refresh rate, for games pacing themselves to it (the player picks it, not the game).
    {XR_TYPE_EXTENSION_PROPERTIES, nullptr, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME, XR_FB_display_refresh_rate_SPEC_VERSION},
};

XrResult XRAPI_CALL rt_xrEnumerateInstanceExtensionProperties(const char* layerName, uint32_t propertyCapacityInput,
                                                             uint32_t* propertyCountOutput,
                                                             XrExtensionProperties* properties) {
    if (layerName) {
        return XR_ERROR_API_LAYER_NOT_PRESENT;
    }
    const uint32_t count = sizeof(kSupportedExtensions) / sizeof(kSupportedExtensions[0]);
    if (!propertyCountOutput) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    *propertyCountOutput = count;
    if (propertyCapacityInput == 0) {
        return XR_SUCCESS;
    }
    if (propertyCapacityInput < count || !properties) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }
    for (uint32_t i = 0; i < count; ++i) {
        copy_string(properties[i].extensionName, XR_MAX_EXTENSION_NAME_SIZE, kSupportedExtensions[i].extensionName);
        properties[i].extensionVersion = kSupportedExtensions[i].extensionVersion;
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrEnumerateApiLayerProperties(uint32_t, uint32_t* propertyCountOutput, XrApiLayerProperties*) {
    if (!propertyCountOutput) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    *propertyCountOutput = 0;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrCreateInstance(const XrInstanceCreateInfo* createInfo, XrInstance* instance) {
    ScopedLock lock(g_lock);
    if (!createInfo || !instance) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    const XrApplicationInfo& app = createInfo->applicationInfo;
    for (uint32_t i = 0; i < createInfo->enabledExtensionCount; ++i) {
        bool supported = false;
        for (const XrExtensionProperties& extension : kSupportedExtensions) {
            supported = supported || std::strcmp(extension.extensionName, createInfo->enabledExtensionNames[i]) == 0;
        }
        if (!supported) {
            runtime_logf("xrCreateInstance: extension %s isn't supported", createInfo->enabledExtensionNames[i]);
            return XR_ERROR_EXTENSION_NOT_PRESENT;
        }
    }
    if (g_instance.alive) {
        return XR_ERROR_LIMIT_REACHED;
    }
    if (XR_VERSION_MAJOR(app.apiVersion) != 1 || XR_VERSION_MINOR(app.apiVersion) > 1) {
        return XR_ERROR_API_VERSION_UNSUPPORTED;
    }
    g_instance = Instance{};
    g_instance.alive = true;
    for (uint32_t i = 0; i < createInfo->enabledExtensionCount; ++i) {
        if (std::strcmp(createInfo->enabledExtensionNames[i], XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME) == 0) {
            g_instance.refresh_rate_events = true;
        }
    }
    g_paths.clear();
    read_headset_optics();
    g_view_width = g_full_width;
    g_view_height = g_full_height;
    read_player_choices();
    add_margin();
    read_overrides();
    runtime_logf("instance for \"%s\" (%s), eye %ux%u at %.0f Hz, ipd %.1f mm", app.applicationName, app.engineName,
                 g_view_width, g_view_height, g_display_hz, g_ipd * 1000.0f);
    *instance = reinterpret_cast<XrInstance>(&g_instance);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrDestroyInstance(XrInstance instance) {
    ScopedLock lock(g_lock);
    if (!check_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    streamer_process::stop();
#ifdef __APPLE__
    vision_pro::stop();
#endif
    g_frames.stop();
    g_instance.alive = false;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrGetInstanceProperties(XrInstance instance, XrInstanceProperties* instanceProperties) {
    if (!check_instance(instance) || !instanceProperties) {
        return XR_ERROR_HANDLE_INVALID;
    }
    instanceProperties->runtimeVersion = kRuntimeVersion;
    copy_string(instanceProperties->runtimeName, XR_MAX_RUNTIME_NAME_SIZE, kRuntimeName);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrPollEvent(XrInstance instance, XrEventDataBuffer* eventData) {
    ScopedLock lock(g_lock);
    if (!check_instance(instance) || !eventData) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (g_event_count == 0) {
        return XR_EVENT_UNAVAILABLE;
    }
    *eventData = g_events[g_event_head];
    g_event_head = (g_event_head + 1) % kMaxQueuedEvents;
    --g_event_count;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrResultToString(XrInstance, XrResult value, char buffer[XR_MAX_RESULT_STRING_SIZE]) {
    std::snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, "%s", result_name(value));
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrStructureTypeToString(XrInstance, XrStructureType value,
                                              char buffer[XR_MAX_STRUCTURE_NAME_SIZE]) {
    std::snprintf(buffer, XR_MAX_STRUCTURE_NAME_SIZE, "%s", structure_name(value));
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrStringToPath(XrInstance instance, const char* pathString, XrPath* path) {
    ScopedLock lock(g_lock);
    if (!check_instance(instance) || !pathString || !path) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (std::strlen(pathString) >= XR_MAX_PATH_LENGTH || pathString[0] != '/') {
        return XR_ERROR_PATH_FORMAT_INVALID;
    }
    for (size_t i = 0; i < g_paths.size(); ++i) {
        if (g_paths[i] == pathString) {
            *path = i + 1;
            return XR_SUCCESS;
        }
    }
    g_paths.emplace_back(pathString);
    *path = g_paths.size();
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrPathToString(XrInstance instance, XrPath path, uint32_t bufferCapacityInput,
                                     uint32_t* bufferCountOutput, char* buffer) {
    ScopedLock lock(g_lock);
    if (!check_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (path == XR_NULL_PATH || path > g_paths.size()) {
        return XR_ERROR_PATH_INVALID;
    }
    const std::string& value = g_paths[path - 1];
    const uint32_t needed = static_cast<uint32_t>(value.size()) + 1;
    if (!bufferCountOutput) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    *bufferCountOutput = needed;
    if (bufferCapacityInput == 0) {
        return XR_SUCCESS;
    }
    if (bufferCapacityInput < needed || !buffer) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }
    std::memcpy(buffer, value.c_str(), needed);
    return XR_SUCCESS;
}

// ---------------------------------------------------------------------------
// System
// ---------------------------------------------------------------------------

constexpr XrSystemId kSystemId = 1;

XrResult XRAPI_CALL rt_xrGetSystem(XrInstance instance, const XrSystemGetInfo* getInfo, XrSystemId* systemId) {
    if (!check_instance(instance) || !getInfo || !systemId) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (getInfo->formFactor != XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY) {
        return XR_ERROR_FORM_FACTOR_UNSUPPORTED;
    }
    *systemId = kSystemId;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrGetSystemProperties(XrInstance instance, XrSystemId systemId,
                                            XrSystemProperties* properties) {
    if (!check_instance(instance) || !properties) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    properties->systemId = kSystemId;
    properties->vendorId = 0;
    copy_string(properties->systemName, XR_MAX_SYSTEM_NAME_SIZE, kSystemName);
    properties->graphicsProperties.maxSwapchainImageWidth = kMaxViewSize;
    properties->graphicsProperties.maxSwapchainImageHeight = kMaxViewSize;
    properties->graphicsProperties.maxLayerCount = XR_MIN_COMPOSITION_LAYERS_SUPPORTED;
    properties->trackingProperties.orientationTracking = XR_TRUE;
    properties->trackingProperties.positionTracking = XR_TRUE;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrEnumerateEnvironmentBlendModes(XrInstance instance, XrSystemId systemId,
                                                       XrViewConfigurationType viewConfigurationType,
                                                       uint32_t capacity, uint32_t* countOutput,
                                                       XrEnvironmentBlendMode* modes) {
    if (!check_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    if (viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    }
    const XrEnvironmentBlendMode opaque[] = {XR_ENVIRONMENT_BLEND_MODE_OPAQUE};
    return return_array(capacity, countOutput, modes, opaque, 1);
}

XrResult XRAPI_CALL rt_xrEnumerateViewConfigurations(XrInstance instance, XrSystemId systemId, uint32_t capacity,
                                                    uint32_t* countOutput, XrViewConfigurationType* types) {
    if (!check_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    const XrViewConfigurationType stereo[] = {XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
    return return_array(capacity, countOutput, types, stereo, 1);
}

XrResult XRAPI_CALL rt_xrGetViewConfigurationProperties(XrInstance instance, XrSystemId systemId,
                                                       XrViewConfigurationType viewConfigurationType,
                                                       XrViewConfigurationProperties* configurationProperties) {
    if (!check_instance(instance) || !configurationProperties) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    if (viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    }
    configurationProperties->viewConfigurationType = viewConfigurationType;
    configurationProperties->fovMutable = XR_FALSE;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrEnumerateViewConfigurationViews(XrInstance instance, XrSystemId systemId,
                                                        XrViewConfigurationType viewConfigurationType,
                                                        uint32_t viewCapacityInput, uint32_t* viewCountOutput,
                                                        XrViewConfigurationView* views) {
    if (!check_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    if (viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    }
    if (!viewCountOutput) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    *viewCountOutput = 2;
    if (viewCapacityInput == 0) {
        return XR_SUCCESS;
    }
    if (viewCapacityInput < 2 || !views) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }
    for (uint32_t i = 0; i < 2; ++i) {
        views[i].recommendedImageRectWidth = g_view_width;
        views[i].maxImageRectWidth = kMaxViewSize;
        views[i].recommendedImageRectHeight = g_view_height;
        views[i].maxImageRectHeight = kMaxViewSize;
        views[i].recommendedSwapchainSampleCount = 1;
        views[i].maxSwapchainSampleCount = 1;
    }
    return XR_SUCCESS;
}

#ifdef __APPLE__
XrResult XRAPI_CALL rt_xrGetMetalGraphicsRequirementsKHR(XrInstance instance, XrSystemId systemId,
                                                        XrGraphicsRequirementsMetalKHR* graphicsRequirements) {
    if (!check_instance(instance) || !graphicsRequirements) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    graphicsRequirements->metalDevice = metal_default_device();
    return XR_SUCCESS;
}
#endif

XrResult XRAPI_CALL rt_xrGetVulkanGraphicsRequirements2KHR(XrInstance instance, XrSystemId systemId,
                                                          XrGraphicsRequirementsVulkanKHR* graphicsRequirements) {
    if (!check_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    return vulkan_graphics_requirements(graphicsRequirements);
}

XrResult XRAPI_CALL rt_xrCreateVulkanInstanceKHR(XrInstance instance, const XrVulkanInstanceCreateInfoKHR* createInfo,
                                                VkInstance* vulkanInstance, VkResult* vulkanResult) {
    if (!check_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (createInfo && createInfo->systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    return vulkan_create_instance(createInfo, vulkanInstance, vulkanResult);
}

XrResult XRAPI_CALL rt_xrGetVulkanGraphicsDevice2KHR(XrInstance instance, const XrVulkanGraphicsDeviceGetInfoKHR* getInfo,
                                                    VkPhysicalDevice* vulkanPhysicalDevice) {
    if (!check_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (getInfo && getInfo->systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    return vulkan_graphics_device(getInfo, vulkanPhysicalDevice);
}

XrResult XRAPI_CALL rt_xrCreateVulkanDeviceKHR(XrInstance instance, const XrVulkanDeviceCreateInfoKHR* createInfo,
                                              VkDevice* vulkanDevice, VkResult* vulkanResult) {
    if (!check_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (createInfo && createInfo->systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    return vulkan_create_device(createInfo, vulkanDevice, vulkanResult);
}

#ifdef _WIN32
XrResult XRAPI_CALL rt_xrGetD3D11GraphicsRequirementsKHR(XrInstance instance, XrSystemId systemId,
                                                        XrGraphicsRequirementsD3D11KHR* graphicsRequirements) {
    if (!check_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    return d3d11_graphics_requirements(graphicsRequirements);
}

XrResult XRAPI_CALL rt_xrGetD3D12GraphicsRequirementsKHR(XrInstance instance, XrSystemId systemId,
                                                        XrGraphicsRequirementsD3D12KHR* graphicsRequirements) {
    if (!check_instance(instance)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    return d3d12_graphics_requirements(graphicsRequirements);
}
#endif

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------

void destroy_swapchain_locked(Swapchain& swapchain) {
    if (g_graphics) {
        g_graphics->destroy_images(swapchain.images);
    }
    swapchain.alive = false;
}

XrResult XRAPI_CALL rt_xrCreateSession(XrInstance instance, const XrSessionCreateInfo* createInfo,
                                      XrSession* session) {
    ScopedLock lock(g_lock);
    if (!check_instance(instance) || !createInfo || !session) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (createInfo->systemId != kSystemId) {
        return XR_ERROR_SYSTEM_INVALID;
    }
    if (g_session.alive) {
        return XR_ERROR_LIMIT_REACHED;
    }
    // The graphics the game renders with, from its binding.
    for (const auto* header = static_cast<const XrBaseInStructure*>(createInfo->next); header && !g_graphics; header = header->next) {
#ifdef __APPLE__
        if (header->type == XR_TYPE_GRAPHICS_BINDING_METAL_KHR) {
            const auto* binding = reinterpret_cast<const XrGraphicsBindingMetalKHR*>(header);
            if (binding->commandQueue) {
                g_graphics = make_metal_graphics(binding->commandQueue);
            }
        }
#endif
        if (header->type == XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR) {
            g_graphics = make_vulkan_graphics(*reinterpret_cast<const XrGraphicsBindingVulkanKHR*>(header));
        }
#ifdef _WIN32
        if (header->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
            g_graphics = make_d3d11_graphics(*reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(header));
        }
        if (header->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR) {
            g_graphics = make_d3d12_graphics(*reinterpret_cast<const XrGraphicsBindingD3D12KHR*>(header));
        }
#endif
    }
    if (!g_graphics) {
        runtime_log("xrCreateSession: no graphics binding this runtime can use");
        return XR_ERROR_GRAPHICS_DEVICE_INVALID;
    }
    g_session = Session{};
    g_session.alive = true;
    *session = reinterpret_cast<XrSession>(&g_session);
    queue_session_state(XR_SESSION_STATE_IDLE);
    queue_session_state(XR_SESSION_STATE_READY);
    g_chosen_hz = g_display_hz;
#ifdef __APPLE__
    streamer_process::on_vision_pro(vision_pro::nearby);
#endif
    start_streamer();
#ifdef __APPLE__
    // A Vision Pro, if one turns up: it has the game while it shows it, and Steam Link otherwise.
    vision_pro::start(vision_pro::Hooks{
        [] { return !streamer_process::headset_connected(); },
        [] {
            streamer_process::stop();  // not under the lock: it may wait a moment for the streamer
            ScopedLock lock(g_lock);
            if (g_session.alive) {
                set_display_rate(kVisionProHz);
            }
        },
        [] {
            ScopedLock lock(g_lock);
            if (g_session.alive) {
                set_display_rate(g_chosen_hz);
                start_streamer();
            }
        },
    });
#endif
    runtime_logf("session on %s", g_graphics->device_name().c_str());
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrDestroySession(XrSession session) {
    ScopedLock lock(g_lock);
    if (!check_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    streamer_process::stop();
#ifdef __APPLE__
    vision_pro::stop();
#endif
    g_pending = PendingFrame{};
    if (g_frames.started()) {
        g_graphics->stop_export(g_frames);
        g_frames.stop();
    }
    for (Swapchain& swapchain : g_swapchains) {
        if (swapchain.alive) {
            destroy_swapchain_locked(swapchain);
        }
    }
    g_graphics.reset();
    for (Space& space : g_spaces) {
        space.alive = false;
    }
    g_session = Session{};
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrBeginSession(XrSession session, const XrSessionBeginInfo* beginInfo) {
    ScopedLock lock(g_lock);
    if (!check_session(session) || !beginInfo) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (beginInfo->primaryViewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    }
    if (g_session.running) {
        return XR_ERROR_SESSION_RUNNING;
    }
    if (g_session.state != XR_SESSION_STATE_READY) {
        return XR_ERROR_SESSION_NOT_READY;
    }
    g_session.running = true;
    latency_critical::begin();
    queue_session_state(XR_SESSION_STATE_SYNCHRONIZED);
    queue_session_state(XR_SESSION_STATE_VISIBLE);
    queue_session_state(XR_SESSION_STATE_FOCUSED);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrEndSession(XrSession session) {
    ScopedLock lock(g_lock);
    if (!check_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (!g_session.running) {
        return XR_ERROR_SESSION_NOT_RUNNING;
    }
    if (g_session.state != XR_SESSION_STATE_STOPPING) {
        return XR_ERROR_SESSION_NOT_STOPPING;
    }
    g_session.running = false;
    latency_critical::end();
    queue_session_state(XR_SESSION_STATE_IDLE);
    queue_session_state(XR_SESSION_STATE_EXITING);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrRequestExitSession(XrSession session) {
    ScopedLock lock(g_lock);
    if (!check_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (!g_session.running) {
        return XR_ERROR_SESSION_NOT_RUNNING;
    }
    queue_session_state(XR_SESSION_STATE_STOPPING);
    return XR_SUCCESS;
}

// ---------------------------------------------------------------------------
// Spaces
// ---------------------------------------------------------------------------

const XrReferenceSpaceType kReferenceSpaces[] = {
    XR_REFERENCE_SPACE_TYPE_VIEW,
    XR_REFERENCE_SPACE_TYPE_LOCAL,
    XR_REFERENCE_SPACE_TYPE_STAGE,
    XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR,
};

XrResult XRAPI_CALL rt_xrEnumerateReferenceSpaces(XrSession session, uint32_t capacity, uint32_t* countOutput,
                                                 XrReferenceSpaceType* spaces) {
    if (!check_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    return return_array(capacity, countOutput, spaces, kReferenceSpaces,
                        sizeof(kReferenceSpaces) / sizeof(kReferenceSpaces[0]));
}

XrResult XRAPI_CALL rt_xrCreateReferenceSpace(XrSession session, const XrReferenceSpaceCreateInfo* createInfo,
                                             XrSpace* space) {
    ScopedLock lock(g_lock);
    if (!check_session(session) || !createInfo || !space) {
        return XR_ERROR_HANDLE_INVALID;
    }
    bool supported = false;
    for (XrReferenceSpaceType type : kReferenceSpaces) {
        supported = supported || type == createInfo->referenceSpaceType;
    }
    if (!supported) {
        return XR_ERROR_REFERENCE_SPACE_UNSUPPORTED;
    }
    Space* slot = allocate_slot(g_spaces);
    if (!slot) {
        return XR_ERROR_LIMIT_REACHED;
    }
    slot->reference_type = createInfo->referenceSpaceType;
    slot->pose_in_reference = createInfo->poseInReferenceSpace;
    *space = reinterpret_cast<XrSpace>(slot);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrGetReferenceSpaceBoundsRect(XrSession session, XrReferenceSpaceType, XrExtent2Df* bounds) {
    if (!check_session(session) || !bounds) {
        return XR_ERROR_HANDLE_INVALID;
    }
    bounds->width = 0.0f;
    bounds->height = 0.0f;
    return XR_SPACE_BOUNDS_UNAVAILABLE;
}

XrResult XRAPI_CALL rt_xrCreateActionSpace(XrSession session, const XrActionSpaceCreateInfo* createInfo,
                                          XrSpace* space) {
    ScopedLock lock(g_lock);
    if (!check_session(session) || !createInfo || !space) {
        return XR_ERROR_HANDLE_INVALID;
    }
    Space* slot = allocate_slot(g_spaces);
    if (!slot) {
        return XR_ERROR_LIMIT_REACHED;
    }
    if (!check_action(createInfo->action)) {
        slot->alive = false;
        return XR_ERROR_HANDLE_INVALID;
    }
    slot->is_action_space = true;
    slot->action = createInfo->action;
    slot->subaction_path = createInfo->subactionPath;
    slot->pose_in_reference = createInfo->poseInActionSpace;
    *space = reinterpret_cast<XrSpace>(slot);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrDestroySpace(XrSpace space) {
    ScopedLock lock(g_lock);
    if (!is_live_handle(g_spaces, space)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    reinterpret_cast<Space*>(space)->alive = false;
    return XR_SUCCESS;
}

// A space's origin in the world frame, with the controller's velocity for an
// action space; false while an action space's controller isn't tracked.
bool space_in_world(const Space& space, XrPosef& pose, XrVector3f* linear, XrVector3f* angular) {
    if (!space.is_action_space) {
        pose = space_origin_in_world(space);
        return true;
    }
    XrPosef grip;
    if (!controller_pose(action_space_hand(space), grip, linear, angular)) {
        return false;
    }
    pose = pose_multiply(grip, space.pose_in_reference);
    return true;
}

// Where one space is in another, and how fast it moves there.
void locate(const Space& target, const Space& base, XrSpaceLocationFlags& flags, XrPosef& pose,
            XrSpaceVelocity* velocity = nullptr) {
    XrPosef target_pose;
    XrPosef base_pose;
    XrVector3f linear{0.0f, 0.0f, 0.0f};
    XrVector3f angular{0.0f, 0.0f, 0.0f};
    if (velocity) {
        velocity->velocityFlags = 0;
    }
    if (!space_in_world(target, target_pose, &linear, &angular) || !space_in_world(base, base_pose, nullptr, nullptr)) {
        flags = 0;
        pose = pose_identity();
        return;
    }
    const XrPosef to_base = pose_inverse(base_pose);
    pose = pose_multiply(to_base, target_pose);
    flags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT |
            XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
    if (velocity && target.is_action_space) {
        // The controller's velocities, in the world frame, turned into the base's.
        velocity->linearVelocity = quat_rotate(to_base.orientation, linear);
        velocity->angularVelocity = quat_rotate(to_base.orientation, angular);
        velocity->velocityFlags = XR_SPACE_VELOCITY_LINEAR_VALID_BIT | XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
    }
}

XrResult XRAPI_CALL rt_xrLocateSpace(XrSpace space, XrSpace baseSpace, XrTime, XrSpaceLocation* location) {
    ScopedLock lock(g_lock);
    if (!is_live_handle(g_spaces, space) || !is_live_handle(g_spaces, baseSpace) || !location) {
        return XR_ERROR_HANDLE_INVALID;
    }
    XrSpaceVelocity* velocity = nullptr;
    for (auto* next = static_cast<XrBaseOutStructure*>(location->next); next; next = next->next) {
        if (next->type == XR_TYPE_SPACE_VELOCITY) {
            velocity = reinterpret_cast<XrSpaceVelocity*>(next);
        }
    }
    locate(*reinterpret_cast<Space*>(space), *reinterpret_cast<Space*>(baseSpace), location->locationFlags,
           location->pose, velocity);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrLocateSpaces(XrSession session, const XrSpacesLocateInfo* locateInfo,
                                     XrSpaceLocations* spaceLocations) {
    ScopedLock lock(g_lock);
    if (!check_session(session) || !locateInfo || !spaceLocations ||
        !is_live_handle(g_spaces, locateInfo->baseSpace)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (spaceLocations->locationCount != locateInfo->spaceCount) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    const Space& base = *reinterpret_cast<Space*>(locateInfo->baseSpace);
    for (uint32_t i = 0; i < locateInfo->spaceCount; ++i) {
        if (!is_live_handle(g_spaces, locateInfo->spaces[i])) {
            return XR_ERROR_HANDLE_INVALID;
        }
        XrSpaceLocationData& out = spaceLocations->locations[i];
        locate(*reinterpret_cast<Space*>(locateInfo->spaces[i]), base, out.locationFlags, out.pose);
    }
    return XR_SUCCESS;
}

// ---------------------------------------------------------------------------
// Swapchains
// ---------------------------------------------------------------------------

// BGRA is what the video encoders take, so colour is offered only as that and the streamer's copy
// needs no swizzle. Godot takes it (sRGB-encoded). The backend says which formats those are.
XrResult XRAPI_CALL rt_xrEnumerateSwapchainFormats(XrSession session, uint32_t capacity, uint32_t* countOutput,
                                                  int64_t* formats) {
    ScopedLock lock(g_lock);
    if (!check_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    const std::vector<int64_t>& offered = g_graphics->formats();
    return return_array(capacity, countOutput, formats, offered.data(), static_cast<uint32_t>(offered.size()));
}

XrResult XRAPI_CALL rt_xrCreateSwapchain(XrSession session, const XrSwapchainCreateInfo* createInfo,
                                        XrSwapchain* swapchain) {
    ScopedLock lock(g_lock);
    if (!check_session(session) || !createInfo || !swapchain) {
        return XR_ERROR_HANDLE_INVALID;
    }
    bool supported = false;
    for (int64_t format : g_graphics->formats()) {
        supported = supported || format == createInfo->format;
    }
    if (!supported) {
        return XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
    }
    if (createInfo->faceCount != 1) {
        return XR_ERROR_FEATURE_UNSUPPORTED;
    }
    Swapchain* slot = allocate_slot(g_swapchains);
    if (!slot) {
        return XR_ERROR_LIMIT_REACHED;
    }
    slot->info = *createInfo;
    slot->info.next = nullptr;
    if (!g_graphics->create_images(slot->info, slot->images)) {
        slot->alive = false;
        return XR_ERROR_RUNTIME_FAILURE;
    }
    *swapchain = reinterpret_cast<XrSwapchain>(slot);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrDestroySwapchain(XrSwapchain swapchain) {
    ScopedLock lock(g_lock);
    if (!is_live_handle(g_swapchains, swapchain)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    destroy_swapchain_locked(*reinterpret_cast<Swapchain*>(swapchain));
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrEnumerateSwapchainImages(XrSwapchain swapchain, uint32_t imageCapacityInput,
                                                 uint32_t* imageCountOutput, XrSwapchainImageBaseHeader* images) {
    ScopedLock lock(g_lock);
    if (!is_live_handle(g_swapchains, swapchain)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (!imageCountOutput) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    *imageCountOutput = kSwapchainImageCount;
    if (imageCapacityInput == 0) {
        return XR_SUCCESS;
    }
    if (imageCapacityInput < kSwapchainImageCount || !images) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }
    const Swapchain& chain = *reinterpret_cast<Swapchain*>(swapchain);
    return g_graphics->enumerate_images(chain.images, images);
}

XrResult XRAPI_CALL rt_xrAcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo*,
                                              uint32_t* index) {
    ScopedLock lock(g_lock);
    if (!is_live_handle(g_swapchains, swapchain) || !index) {
        return XR_ERROR_HANDLE_INVALID;
    }
    Swapchain& chain = *reinterpret_cast<Swapchain*>(swapchain);
    if (chain.acquired >= 0) {
        return XR_ERROR_CALL_ORDER_INVALID;
    }
    chain.acquired = static_cast<int>(chain.next);
    chain.next = (chain.next + 1) % kSwapchainImageCount;
    g_graphics->prepare_image(chain.images[chain.acquired]);
    *index = static_cast<uint32_t>(chain.acquired);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrWaitSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageWaitInfo*) {
    // The copy out is queued after the game's own work on the same queue, so
    // the GPU orders them: there's never anything to wait for here.
    return is_live_handle(g_swapchains, swapchain) ? XR_SUCCESS : XR_ERROR_HANDLE_INVALID;
}

XrResult XRAPI_CALL rt_xrReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo*) {
    ScopedLock lock(g_lock);
    if (!is_live_handle(g_swapchains, swapchain)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    Swapchain& chain = *reinterpret_cast<Swapchain*>(swapchain);
    if (chain.acquired < 0) {
        return XR_ERROR_CALL_ORDER_INVALID;
    }
    chain.last_released = chain.acquired;
    chain.acquired = -1;
    return XR_SUCCESS;
}

// ---------------------------------------------------------------------------
// Frame loop
// ---------------------------------------------------------------------------

XrResult XRAPI_CALL rt_xrWaitFrame(XrSession session, const XrFrameWaitInfo*, XrFrameState* frameState) {
    XrTime target_display;
    {
        ScopedLock lock(g_lock);
        if (!check_session(session) || !frameState) {
            return XR_ERROR_HANDLE_INVALID;
        }
        if (!g_session.running) {
            return XR_ERROR_SESSION_NOT_RUNNING;
        }
        send_pending_frame();  // the last frame's drawing is committed by now
        const XrTime now = now_ns();
        target_display = g_session.last_predicted_display + g_frame_period_ns;
        if (target_display < now) {
            // Behind: skip whole periods so the cadence keeps its phase.
            target_display += ((now - target_display) / g_frame_period_ns + 1) * g_frame_period_ns;
        }
        g_session.last_predicted_display = target_display;
    }
    // Back one period before the display time, so the game has a whole frame.
    sleep_until(target_display - g_frame_period_ns);
    frameState->predictedDisplayTime = target_display;
    frameState->predictedDisplayPeriod = g_frame_period_ns;
    frameState->shouldRender = XR_TRUE;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrBeginFrame(XrSession session, const XrFrameBeginInfo*) {
    ScopedLock lock(g_lock);
    if (!check_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (!g_session.running) {
        return XR_ERROR_SESSION_NOT_RUNNING;
    }
    const bool discarded = g_session.frame_begun;
    g_session.frame_begun = true;
    return discarded ? XR_FRAME_DISCARDED : XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrLocateViews(XrSession session, const XrViewLocateInfo* viewLocateInfo, XrViewState* viewState,
                                    uint32_t viewCapacityInput, uint32_t* viewCountOutput, XrView* views) {
    ScopedLock lock(g_lock);
    if (!check_session(session) || !viewLocateInfo || !viewState || !viewCountOutput) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (viewLocateInfo->viewConfigurationType != XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) {
        return XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED;
    }
    if (!is_live_handle(g_spaces, viewLocateInfo->space)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    *viewCountOutput = 2;
    if (viewCapacityInput == 0) {
        return XR_SUCCESS;
    }
    if (viewCapacityInput < 2 || !views) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }
    // The game may ask several times for one display time (culling, then
    // drawing): each must get the same pose, or the frame is drawn from one
    // sample and stamped with another and the headset's reprojection warps it.
    LocatedFrame* located = nullptr;
    for (LocatedFrame& candidate : g_located) {
        if (candidate.display_time == viewLocateInfo->displayTime) {
            located = &candidate;
        }
    }
    if (located) {
        g_head_pose = located->head;
        g_last_tracking_timestamp_ns = located->tracking_timestamp_ns;
    } else {
        refresh_tracking();
        located = &g_located[g_located_next];
        *located = LocatedFrame{viewLocateInfo->displayTime, g_last_tracking_timestamp_ns, g_head_pose};
        g_located_next = (g_located_next + 1) % (sizeof(g_located) / sizeof(g_located[0]));
    }
    const Space& base = *reinterpret_cast<Space*>(viewLocateInfo->space);
    Space view_space{};
    view_space.alive = true;
    view_space.reference_type = XR_REFERENCE_SPACE_TYPE_VIEW;
    const XrPosef head = pose_multiply(pose_inverse(space_origin_in_world(base)), space_origin_in_world(view_space));
    for (uint32_t eye = 0; eye < 2; ++eye) {
        XrPosef offset = pose_identity();
        offset.position.x = (eye == 0 ? -0.5f : 0.5f) * g_ipd;
        views[eye].pose = pose_multiply(head, offset);
        views[eye].fov = g_fov[eye];
    }
    viewState->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT |
                                XR_VIEW_STATE_ORIENTATION_TRACKED_BIT | XR_VIEW_STATE_POSITION_TRACKED_BIT;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrEndFrame(XrSession session, const XrFrameEndInfo* frameEndInfo) {
    const uint64_t entered_at_unix_ns = platform::unix_ns();
    ScopedLock lock(g_lock);
    if (!check_session(session) || !frameEndInfo) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (!g_session.running) {
        return XR_ERROR_SESSION_NOT_RUNNING;
    }
    if (!g_session.frame_begun) {
        return XR_ERROR_CALL_ORDER_INVALID;
    }
    g_session.frame_begun = false;
    const uint64_t frame = ++g_session.frame_count;

    for (uint32_t i = 0; i < frameEndInfo->layerCount; ++i) {
        const XrCompositionLayerBaseHeader* layer = frameEndInfo->layers[i];
        if (!layer) {
            return XR_ERROR_LAYER_INVALID;
        }
        if (layer->type != XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
            continue;  // only the world goes to the headset; the game draws its panels into it
        }
        const auto* projection = reinterpret_cast<const XrCompositionLayerProjection*>(layer);
        if (!is_live_handle(g_spaces, projection->space)) {
            return XR_ERROR_HANDLE_INVALID;
        }
        if (projection->viewCount < 2) {
            continue;
        }
        Graphics::EyeSource eyes[2];
        SharedFrames::Frame exported;
        bool exportable = true;
        for (uint32_t v = 0; v < 2; ++v) {
            const XrCompositionLayerProjectionView& view = projection->views[v];
            if (!is_live_handle(g_swapchains, view.subImage.swapchain)) {
                return XR_ERROR_HANDLE_INVALID;
            }
            const Swapchain& chain = *reinterpret_cast<Swapchain*>(view.subImage.swapchain);
            // The spec's checks: a layer and a rectangle inside the swapchain (the copy reads there).
            const XrRect2Di& rect = view.subImage.imageRect;
            if (view.subImage.imageArrayIndex >= chain.info.arraySize || rect.offset.x < 0 || rect.offset.y < 0 || rect.extent.width <= 0 ||
                rect.extent.height <= 0 || static_cast<uint64_t>(rect.offset.x) + rect.extent.width > chain.info.width ||
                static_cast<uint64_t>(rect.offset.y) + rect.extent.height > chain.info.height) {
                return XR_ERROR_VALIDATION_FAILURE;
            }
            // The stream takes each eye whole, at the size it recommended.
            exportable = exportable && chain.last_released >= 0 && chain.info.width == g_view_width &&
                         chain.info.height == g_view_height && chain.info.sampleCount == 1 && rect.offset.x == 0 && rect.offset.y == 0 &&
                         static_cast<uint32_t>(rect.extent.width) == g_view_width && static_cast<uint32_t>(rect.extent.height) == g_view_height;
            if (!exportable) {
                break;
            }
            eyes[v].image = chain.images[chain.last_released];
            eyes[v].array_index = view.subImage.imageArrayIndex;
            SharedFrames::View& out = exported.views[v];
            out.orientation[0] = view.pose.orientation.x;
            out.orientation[1] = view.pose.orientation.y;
            out.orientation[2] = view.pose.orientation.z;
            out.orientation[3] = view.pose.orientation.w;
            out.position[0] = view.pose.position.x;
            out.position[1] = view.pose.position.y;
            out.position[2] = view.pose.position.z;
            out.fov[0] = view.fov.angleLeft;
            out.fov[1] = view.fov.angleRight;
            out.fov[2] = view.fov.angleUp;
            out.fov[3] = view.fov.angleDown;
        }
        if (!exportable) {
            // Said once, whenever it first happens (the first frames often have no layers at all).
            static bool said = false;
            if (!said) {
                said = true;
                runtime_logf("frames aren't sent: each eye must be a whole swapchain image of %ux%u, one sample (the size xrEnumerateViewConfigurationViews recommends)",
                             g_view_width, g_view_height);
            }
            continue;
        }
        if (!g_frames.started()) {
            if (g_session.export_failed) {
                continue;
            }
            const Swapchain& first = *reinterpret_cast<Swapchain*>(projection->views[0].subImage.swapchain);
            if (!g_frames.start(g_view_width, g_view_height, g_graphics->shared_format(first.info.format))) {
                g_session.export_failed = true;
                continue;
            }
            if (!g_graphics->start_export(g_frames)) {
                // Whatever it set up before failing is freed.
                g_graphics->stop_export(g_frames);
                g_frames.stop();
                g_session.export_failed = true;
                runtime_log("frames aren't sent: the frame copies couldn't be set up");
                continue;
            }
        }
        g_frames.note_frame_submitted();
        if (!g_frames.reader_present(static_cast<uint64_t>(now_ns()))) {
            continue;  // no headset watching: nothing to copy
        }
        uint64_t tracking_timestamp_ns = g_last_tracking_timestamp_ns;
        for (const LocatedFrame& located : g_located) {
            if (located.display_time == frameEndInfo->displayTime) {
                tracking_timestamp_ns = located.tracking_timestamp_ns;
            }
        }
        exported.frame_id = static_cast<uint32_t>(frame);
        exported.predicted_display_time_ns = static_cast<uint64_t>(frameEndInfo->displayTime);
        exported.submitted_at_unix_ns = entered_at_unix_ns;
        exported.tracking_timestamp_ns = tracking_timestamp_ns;
        if (g_graphics->copies_after_end_frame()) {
            g_pending.waiting = true;
            g_pending.eyes[0] = eyes[0];
            g_pending.eyes[1] = eyes[1];
            g_pending.frame = exported;
        } else {
            g_graphics->export_frame(g_frames, eyes, exported);
        }
    }
    return XR_SUCCESS;
}

// ---------------------------------------------------------------------------
// Input: the controllers, through the streamer, as OpenXR's Meta Touch
// controller (every headset's controllers are mapped onto it). Games that
// only suggest the simple controller get that instead. PURR_VR_CONTROLLERS=0
// (Purr's xr/controllers setting) turns them off: actions are never
// active and no interaction profile is current.
// ---------------------------------------------------------------------------

constexpr char kTouchProfile[] = "/interaction_profiles/oculus/touch_controller";
constexpr char kSimpleProfile[] = "/interaction_profiles/khr/simple_controller";
constexpr char kHandPaths[2][24] = {"/user/hand/left", "/user/hand/right"};

bool controllers_enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("PURR_VR_CONTROLLERS");
        return !(value && std::strcmp(value, "0") == 0);
    }();
    return enabled;
}

// The controllers as of the last xrSyncActions, and the sync before.
vr_shared::InputState g_inputs{};
vr_shared::InputState g_previous_inputs{};
XrPath g_current_profile[2] = {XR_NULL_PATH, XR_NULL_PATH};

// A byte copy: the block's atomic sequence only matters in shared memory.
void copy_inputs(vr_shared::InputState& to, const vr_shared::InputState& from) {
    std::memcpy(static_cast<void*>(&to), static_cast<const void*>(&from), sizeof(to));
}

XrPath path_of(const char* text) {
    for (size_t i = 0; i < g_paths.size(); ++i) {
        if (g_paths[i] == text) {
            return i + 1;
        }
    }
    g_paths.emplace_back(text);
    return g_paths.size();
}

const std::string* path_text(XrPath path) {
    return path != XR_NULL_PATH && path <= g_paths.size() ? &g_paths[path - 1] : nullptr;
}

// What a binding path reads: a hand, and an input, a pose or the haptics.
struct Source {
    int hand = -1;
    enum Kind { kNone, kValue, kStick, kPose, kHaptic } kind = kNone;
    int input = 0;            // for kValue
    bool from_simple = false; // simple controller's select: the trigger, as a click
};

Source source_of(XrPath binding) {
    Source source;
    const std::string* text = path_text(binding);
    if (!text) {
        return source;
    }
    for (int hand = 0; hand < 2; ++hand) {
        const std::string prefix = std::string(kHandPaths[hand]) + "/";
        if (text->compare(0, prefix.size(), prefix) == 0) {
            source.hand = hand;
        }
    }
    if (source.hand < 0) {
        return source;
    }
    const std::string component = text->substr(std::strlen(kHandPaths[source.hand]) + 1);
    struct Entry {
        const char* component;
        int input;
    };
    static const Entry kValues[] = {
        {"input/menu/click", vr_shared::kInputMenuClick},     {"input/system/click", vr_shared::kInputSystemClick},
        {"input/a/click", vr_shared::kInputAClick},           {"input/a/touch", vr_shared::kInputATouch},
        {"input/b/click", vr_shared::kInputBClick},           {"input/b/touch", vr_shared::kInputBTouch},
        {"input/x/click", vr_shared::kInputXClick},           {"input/x/touch", vr_shared::kInputXTouch},
        {"input/y/click", vr_shared::kInputYClick},           {"input/y/touch", vr_shared::kInputYTouch},
        {"input/squeeze", vr_shared::kInputSqueezeValue},     {"input/squeeze/value", vr_shared::kInputSqueezeValue},
        {"input/trigger", vr_shared::kInputTriggerValue},     {"input/trigger/value", vr_shared::kInputTriggerValue},
        {"input/trigger/touch", vr_shared::kInputTriggerTouch},
        {"input/thumbstick/x", vr_shared::kInputThumbstickX}, {"input/thumbstick/y", vr_shared::kInputThumbstickY},
        {"input/thumbstick/click", vr_shared::kInputThumbstickClick},
        {"input/thumbstick/touch", vr_shared::kInputThumbstickTouch},
        {"input/thumbrest/touch", vr_shared::kInputThumbrestTouch},
        {"input/select/click", vr_shared::kInputTriggerValue},
    };
    if (component == "input/thumbstick") {
        source.kind = Source::kStick;
    } else if (component == "input/grip/pose" || component == "input/aim/pose" || component == "input/grip" ||
               component == "input/aim") {
        // The headset sends the grip pose only: the aim pose is the same for now.
        source.kind = Source::kPose;
    } else if (component == "output/haptic") {
        source.kind = Source::kHaptic;
    } else {
        for (const Entry& entry : kValues) {
            if (component == entry.component) {
                source.kind = Source::kValue;
                source.input = entry.input;
                source.from_simple = component == "input/select/click";
            }
        }
    }
    return source;
}

// The bindings in use: the Touch controller's if the game suggested any,
// otherwise the simple controller's.
bool use_touch_profile() {
    for (const Action& action : g_actions) {
        if (action.alive && !action.touch_bindings.empty()) {
            return true;
        }
    }
    return false;
}

const std::vector<XrPath>& bindings_of(const Action& action) {
    return use_touch_profile() ? action.touch_bindings : action.simple_bindings;
}

// The hand a subaction path names (-1 for none or any).
int hand_of(XrPath subaction_path) {
    const std::string* text = path_text(subaction_path);
    for (int hand = 0; hand < 2 && text; ++hand) {
        if (*text == kHandPaths[hand]) {
            return hand;
        }
    }
    return -1;
}

bool hand_active(const vr_shared::InputState& inputs, int hand) {
    return controllers_enabled() && g_session.state == XR_SESSION_STATE_FOCUSED && inputs.hands[hand].active != 0;
}

float input_value(const vr_shared::InputState& inputs, const Source& source) {
    return inputs.hands[source.hand].inputs[source.input];
}

// An action's value on one hand (or both, hand -1) from a snapshot of the
// controllers, and whether any binding is active.
struct Evaluated {
    bool active = false;
    bool boolean = false;
    float value = 0.0f;
    XrVector2f vector{0.0f, 0.0f};
};

Evaluated evaluate(const Action& action, int hand, const vr_shared::InputState& inputs, bool was_on) {
    Evaluated out;
    for (XrPath binding : bindings_of(action)) {
        const Source source = source_of(binding);
        if (source.hand < 0 || (hand >= 0 && source.hand != hand) || !hand_active(inputs, source.hand)) {
            continue;
        }
        if (source.kind == Source::kValue) {
            const float value = input_value(inputs, source);
            out.active = true;
            if (std::fabs(value) > std::fabs(out.value)) {
                out.value = value;
            }
            // A pressed trigger or squeeze counts as a click past half way,
            // with a little hysteresis so it doesn't flicker.
            out.boolean = out.boolean || value > (was_on ? 0.45f : 0.55f);
        } else if (source.kind == Source::kStick) {
            const auto& hand_inputs = inputs.hands[source.hand].inputs;
            const XrVector2f stick{hand_inputs[vr_shared::kInputThumbstickX], hand_inputs[vr_shared::kInputThumbstickY]};
            out.active = true;
            if (stick.x * stick.x + stick.y * stick.y > out.vector.x * out.vector.x + out.vector.y * out.vector.y) {
                out.vector = stick;
            }
        } else if (source.kind == Source::kPose) {
            out.active = true;
        }
    }
    return out;
}

bool check_action(XrAction action) {
    return is_live_handle(g_actions, action);
}

// Takes the newest controllers, and tells the game when they appear or go.
void refresh_controllers() {
    copy_inputs(g_previous_inputs, g_inputs);
    vr_shared::InputState state{};
    if (g_frames.read_input_state(state)) {
        copy_inputs(g_inputs, state);
    }
    for (int hand = 0; hand < 2; ++hand) {
        const bool connected = controllers_enabled() && g_inputs.hands[hand].active != 0;
        const XrPath profile = connected ? path_of(use_touch_profile() ? kTouchProfile : kSimpleProfile) : XR_NULL_PATH;
        if (profile != g_current_profile[hand]) {
            g_current_profile[hand] = profile;
            if (g_event_count < kMaxQueuedEvents && g_session.alive) {
                XrEventDataBuffer& slot = g_events[(g_event_head + g_event_count) % kMaxQueuedEvents];
                std::memset(&slot, 0, sizeof(slot));
                auto* event = reinterpret_cast<XrEventDataInteractionProfileChanged*>(&slot);
                event->type = XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED;
                event->session = reinterpret_cast<XrSession>(&g_session);
                ++g_event_count;
            }
        }
    }
}

// The grip pose of a hand's controller in the world frame (STAGE).
bool controller_pose(int hand, XrPosef& pose, XrVector3f* linear, XrVector3f* angular) {
    vr_shared::InputState state{};
    if (!g_frames.read_input_state(state)) {
        copy_inputs(state, g_inputs);
    }
    if (hand < 0 || !hand_active(state, hand)) {
        return false;
    }
    const vr_shared::ControllerState& c = state.hands[hand];
    pose.orientation = XrQuaternionf{c.orientation[0], c.orientation[1], c.orientation[2], c.orientation[3]};
    pose.position = XrVector3f{c.position[0], c.position[1], c.position[2]};
    if (linear) {
        *linear = XrVector3f{c.linear_velocity[0], c.linear_velocity[1], c.linear_velocity[2]};
    }
    if (angular) {
        *angular = XrVector3f{c.angular_velocity[0], c.angular_velocity[1], c.angular_velocity[2]};
    }
    return true;
}

// The hand an action space follows: its subaction path's, or the one its
// action's pose bindings name.
int action_space_hand(const Space& space) {
    if (!check_action(space.action)) {
        return -1;
    }
    int hand = hand_of(space.subaction_path);
    if (hand >= 0) {
        return hand;
    }
    for (XrPath binding : bindings_of(*reinterpret_cast<Action*>(space.action))) {
        const Source source = source_of(binding);
        if (source.kind == Source::kPose) {
            return source.hand;
        }
    }
    return -1;
}

XrResult XRAPI_CALL rt_xrCreateActionSet(XrInstance instance, const XrActionSetCreateInfo* createInfo,
                                        XrActionSet* actionSet) {
    ScopedLock lock(g_lock);
    if (!check_instance(instance) || !createInfo || !actionSet) {
        return XR_ERROR_HANDLE_INVALID;
    }
    ActionSet* slot = allocate_slot(g_action_sets);
    if (!slot) {
        return XR_ERROR_LIMIT_REACHED;
    }
    *actionSet = reinterpret_cast<XrActionSet>(slot);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrDestroyActionSet(XrActionSet actionSet) {
    ScopedLock lock(g_lock);
    if (!is_live_handle(g_action_sets, actionSet)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    reinterpret_cast<ActionSet*>(actionSet)->alive = false;
    for (Action& action : g_actions) {
        if (action.alive && action.set == actionSet) {
            action.alive = false;
        }
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrCreateAction(XrActionSet actionSet, const XrActionCreateInfo* createInfo, XrAction* action) {
    ScopedLock lock(g_lock);
    if (!is_live_handle(g_action_sets, actionSet) || !createInfo || !action) {
        return XR_ERROR_HANDLE_INVALID;
    }
    Action* slot = allocate_slot(g_actions);
    if (!slot) {
        return XR_ERROR_LIMIT_REACHED;
    }
    slot->set = actionSet;
    slot->type = createInfo->actionType;
    for (uint32_t i = 0; i < createInfo->countSubactionPaths && createInfo->subactionPaths; ++i) {
        slot->subaction_paths.push_back(createInfo->subactionPaths[i]);
    }
    *action = reinterpret_cast<XrAction>(slot);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrDestroyAction(XrAction action) {
    ScopedLock lock(g_lock);
    if (!check_action(action)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    reinterpret_cast<Action*>(action)->alive = false;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrSuggestInteractionProfileBindings(XrInstance instance,
                                                          const XrInteractionProfileSuggestedBinding* bindings) {
    ScopedLock lock(g_lock);
    if (!check_instance(instance) || !bindings) {
        return XR_ERROR_HANDLE_INVALID;
    }
    const std::string* profile = path_text(bindings->interactionProfile);
    if (!profile) {
        return XR_ERROR_PATH_INVALID;
    }
    const bool touch = *profile == kTouchProfile;
    const bool simple = *profile == kSimpleProfile;
    if (!touch && !simple) {
        return XR_SUCCESS;  // a controller this runtime never reports: accepted, unused
    }
    for (uint32_t i = 0; i < bindings->countSuggestedBindings; ++i) {
        const XrActionSuggestedBinding& binding = bindings->suggestedBindings[i];
        if (!check_action(binding.action)) {
            return XR_ERROR_HANDLE_INVALID;
        }
        // Inputs the streamer doesn't carry (an extension's palm pose, say)
        // are accepted and never active, as on a controller without them.
        if (source_of(binding.binding).kind == Source::kNone) {
            const std::string* text = path_text(binding.binding);
            runtime_logf("binding %s isn't streamed, so its action stays inactive", text ? text->c_str() : "(invalid)");
        }
    }
    // Each suggestion replaces that profile's earlier one.
    for (Action& action : g_actions) {
        (touch ? action.touch_bindings : action.simple_bindings).clear();
    }
    for (uint32_t i = 0; i < bindings->countSuggestedBindings; ++i) {
        const XrActionSuggestedBinding& binding = bindings->suggestedBindings[i];
        Action& action = *reinterpret_cast<Action*>(binding.action);
        (touch ? action.touch_bindings : action.simple_bindings).push_back(binding.binding);
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrAttachSessionActionSets(XrSession session, const XrSessionActionSetsAttachInfo* attachInfo) {
    return check_session(session) && attachInfo ? XR_SUCCESS : XR_ERROR_HANDLE_INVALID;
}

XrResult XRAPI_CALL rt_xrGetCurrentInteractionProfile(XrSession session, XrPath topLevelUserPath,
                                                     XrInteractionProfileState* interactionProfile) {
    ScopedLock lock(g_lock);
    if (!check_session(session) || !interactionProfile) {
        return XR_ERROR_HANDLE_INVALID;
    }
    const int hand = hand_of(topLevelUserPath);
    interactionProfile->interactionProfile = hand >= 0 ? g_current_profile[hand] : XR_NULL_PATH;
    return XR_SUCCESS;
}

// Reads an action's state for XrActionStateGetInfo's subaction path.
template <typename Fill>
XrResult get_action_state(XrSession session, const XrActionStateGetInfo* getInfo, XrActionType type, Fill fill) {
    ScopedLock lock(g_lock);
    if (!check_session(session) || !getInfo || !check_action(getInfo->action)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    Action& action = *reinterpret_cast<Action*>(getInfo->action);
    if (action.type != type) {
        return XR_ERROR_ACTION_TYPE_MISMATCH;
    }
    const int hand = hand_of(getInfo->subactionPath);
    if (getInfo->subactionPath != XR_NULL_PATH && hand < 0) {
        return XR_ERROR_PATH_UNSUPPORTED;
    }
    fill(action, hand);
    return XR_SUCCESS;
}

// The index into an action's per-hand history: a hand, or 0 for both.
int history_slot(int hand) {
    return hand < 0 ? 0 : hand;
}

XrResult XRAPI_CALL rt_xrGetActionStateBoolean(XrSession session, const XrActionStateGetInfo* getInfo,
                                              XrActionStateBoolean* state) {
    if (!state) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    return get_action_state(session, getInfo, XR_ACTION_TYPE_BOOLEAN_INPUT, [&](Action& action, int hand) {
        const int slot = history_slot(hand);
        const Evaluated now = evaluate(action, hand, g_inputs, action.last_bool[slot]);
        state->isActive = now.active;
        state->currentState = now.boolean;
        state->changedSinceLastSync = now.boolean != evaluate(action, hand, g_previous_inputs, !now.boolean).boolean;
        state->lastChangeTime = action.last_change[slot];
    });
}

XrResult XRAPI_CALL rt_xrGetActionStateFloat(XrSession session, const XrActionStateGetInfo* getInfo,
                                            XrActionStateFloat* state) {
    if (!state) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    return get_action_state(session, getInfo, XR_ACTION_TYPE_FLOAT_INPUT, [&](Action& action, int hand) {
        const int slot = history_slot(hand);
        const Evaluated now = evaluate(action, hand, g_inputs, false);
        state->isActive = now.active;
        state->currentState = now.value;
        state->changedSinceLastSync = now.value != evaluate(action, hand, g_previous_inputs, false).value;
        state->lastChangeTime = action.last_change[slot];
    });
}

XrResult XRAPI_CALL rt_xrGetActionStateVector2f(XrSession session, const XrActionStateGetInfo* getInfo,
                                               XrActionStateVector2f* state) {
    if (!state) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    return get_action_state(session, getInfo, XR_ACTION_TYPE_VECTOR2F_INPUT, [&](Action& action, int hand) {
        const int slot = history_slot(hand);
        const Evaluated now = evaluate(action, hand, g_inputs, false);
        const Evaluated before = evaluate(action, hand, g_previous_inputs, false);
        state->isActive = now.active;
        state->currentState = now.vector;
        state->changedSinceLastSync = now.vector.x != before.vector.x || now.vector.y != before.vector.y;
        state->lastChangeTime = action.last_change[slot];
    });
}

XrResult XRAPI_CALL rt_xrGetActionStatePose(XrSession session, const XrActionStateGetInfo* getInfo,
                                           XrActionStatePose* state) {
    if (!state) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    return get_action_state(session, getInfo, XR_ACTION_TYPE_POSE_INPUT, [&](Action& action, int hand) {
        state->isActive = evaluate(action, hand, g_inputs, false).active;
    });
}

XrResult XRAPI_CALL rt_xrSyncActions(XrSession session, const XrActionsSyncInfo* syncInfo) {
    ScopedLock lock(g_lock);
    if (!check_session(session) || !syncInfo) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (g_session.state != XR_SESSION_STATE_FOCUSED) {
        return XR_SESSION_NOT_FOCUSED;
    }
    refresh_controllers();
    // When each action last changed, by hand and for both.
    const XrTime now = now_ns();
    for (Action& action : g_actions) {
        if (!action.alive) {
            continue;
        }
        for (int hand = -1; hand < 2; ++hand) {
            const int slot = history_slot(hand);
            if (hand == 0 && action.subaction_paths.empty()) {
                continue;  // slot 0 is both hands for an action without subaction paths
            }
            const Evaluated value = evaluate(action, hand, g_inputs, action.last_bool[slot]);
            if (value.boolean != action.last_bool[slot] || value.value != action.last_float[slot] ||
                value.vector.x != action.last_vector[slot].x || value.vector.y != action.last_vector[slot].y) {
                action.last_change[slot] = now;
            }
            action.last_bool[slot] = value.boolean;
            action.last_float[slot] = value.value;
            action.last_vector[slot] = value.vector;
        }
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrEnumerateBoundSourcesForAction(XrSession session, const XrBoundSourcesForActionEnumerateInfo* enumerateInfo,
                                                       uint32_t capacity, uint32_t* sourceCountOutput, XrPath* sources) {
    ScopedLock lock(g_lock);
    if (!check_session(session) || !enumerateInfo || !sourceCountOutput || !check_action(enumerateInfo->action)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    std::vector<XrPath> bound;
    for (XrPath binding : bindings_of(*reinterpret_cast<Action*>(enumerateInfo->action))) {
        const Source source = source_of(binding);
        if (source.hand >= 0 && hand_active(g_inputs, source.hand)) {
            bound.push_back(binding);
        }
    }
    return return_array(capacity, sourceCountOutput, sources, bound.data(), static_cast<uint32_t>(bound.size()));
}

XrResult XRAPI_CALL rt_xrGetInputSourceLocalizedName(XrSession session, const XrInputSourceLocalizedNameGetInfo*,
                                                    uint32_t bufferCapacityInput, uint32_t* bufferCountOutput,
                                                    char* buffer) {
    if (!check_session(session) || !bufferCountOutput) {
        return XR_ERROR_HANDLE_INVALID;
    }
    *bufferCountOutput = 1;
    if (bufferCapacityInput >= 1 && buffer) {
        buffer[0] = '\0';
    }
    return XR_SUCCESS;
}

// Vibrates the hands an action's haptic bindings name (or the subaction
// path's hand), through the streamer.
XrResult haptic(XrSession session, const XrHapticActionInfo* info, float duration_s, float frequency, float amplitude) {
    ScopedLock lock(g_lock);
    if (!check_session(session) || !info || !check_action(info->action)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    const Action& action = *reinterpret_cast<Action*>(info->action);
    if (action.type != XR_ACTION_TYPE_VIBRATION_OUTPUT) {
        return XR_ERROR_ACTION_TYPE_MISMATCH;
    }
    const int only = hand_of(info->subactionPath);
    for (XrPath binding : bindings_of(action)) {
        const Source source = source_of(binding);
        if (source.kind == Source::kHaptic && (only < 0 || source.hand == only) && hand_active(g_inputs, source.hand)) {
            g_frames.write_haptic(source.hand, duration_s, frequency, amplitude);
        }
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrApplyHapticFeedback(XrSession session, const XrHapticActionInfo* info,
                                            const XrHapticBaseHeader* feedback) {
    if (!feedback || feedback->type != XR_TYPE_HAPTIC_VIBRATION) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    const auto* vibration = reinterpret_cast<const XrHapticVibration*>(feedback);
    const float duration_s = vibration->duration > 0 ? static_cast<float>(vibration->duration) / 1e9f : 0.0f;
    return haptic(session, info, duration_s, vibration->frequency > 0.0f ? vibration->frequency : 0.0f,
                  std::fmin(std::fmax(vibration->amplitude, 0.0f), 1.0f));
}

XrResult XRAPI_CALL rt_xrStopHapticFeedback(XrSession session, const XrHapticActionInfo* info) {
    return haptic(session, info, 0.0f, 0.0f, 0.0f);
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

struct Entry {
    const char* name;
    PFN_xrVoidFunction function;
    bool needs_instance;
};

#define RT_ENTRY(name, needs_instance) {#name, reinterpret_cast<PFN_xrVoidFunction>(rt_##name), needs_instance}

// ---------------------------------------------------------------------------
// Display refresh rate (XR_FB_display_refresh_rate): the one rate the headset runs at now.
// ---------------------------------------------------------------------------

XrResult XRAPI_CALL rt_xrEnumerateDisplayRefreshRatesFB(XrSession session, uint32_t displayRefreshRateCapacityInput,
                                                       uint32_t* displayRefreshRateCountOutput, float* displayRefreshRates) {
    if (!check_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (!displayRefreshRateCountOutput) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    *displayRefreshRateCountOutput = 1;
    if (displayRefreshRateCapacityInput == 0) {
        return XR_SUCCESS;
    }
    if (!displayRefreshRates) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    displayRefreshRates[0] = g_display_hz;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrGetDisplayRefreshRateFB(XrSession session, float* displayRefreshRate) {
    if (!check_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (!displayRefreshRate) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    *displayRefreshRate = g_display_hz;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL rt_xrRequestDisplayRefreshRateFB(XrSession session, float displayRefreshRate) {
    if (!check_session(session)) {
        return XR_ERROR_HANDLE_INVALID;
    }
    // 0 asks for the runtime's choice; only the rate the player chose is on offer.
    return displayRefreshRate == 0.0f || displayRefreshRate == g_display_hz ? XR_SUCCESS : XR_ERROR_DISPLAY_REFRESH_RATE_UNSUPPORTED_FB;
}

const Entry kEntries[] = {
    RT_ENTRY(xrEnumerateDisplayRefreshRatesFB, true),
    RT_ENTRY(xrGetDisplayRefreshRateFB, true),
    RT_ENTRY(xrRequestDisplayRefreshRateFB, true),
    RT_ENTRY(xrEnumerateInstanceExtensionProperties, false),
    RT_ENTRY(xrEnumerateApiLayerProperties, false),
    RT_ENTRY(xrCreateInstance, false),
    RT_ENTRY(xrDestroyInstance, true),
    RT_ENTRY(xrGetInstanceProperties, true),
    RT_ENTRY(xrPollEvent, true),
    RT_ENTRY(xrResultToString, true),
    RT_ENTRY(xrStructureTypeToString, true),
    RT_ENTRY(xrStringToPath, true),
    RT_ENTRY(xrPathToString, true),
    RT_ENTRY(xrGetSystem, true),
    RT_ENTRY(xrGetSystemProperties, true),
    RT_ENTRY(xrEnumerateEnvironmentBlendModes, true),
    RT_ENTRY(xrEnumerateViewConfigurations, true),
    RT_ENTRY(xrGetViewConfigurationProperties, true),
    RT_ENTRY(xrEnumerateViewConfigurationViews, true),
#ifdef __APPLE__
    RT_ENTRY(xrGetMetalGraphicsRequirementsKHR, true),
#endif
    RT_ENTRY(xrGetVulkanGraphicsRequirements2KHR, true),
    RT_ENTRY(xrCreateVulkanInstanceKHR, true),
    RT_ENTRY(xrGetVulkanGraphicsDevice2KHR, true),
    RT_ENTRY(xrCreateVulkanDeviceKHR, true),
#ifdef _WIN32
    RT_ENTRY(xrGetD3D11GraphicsRequirementsKHR, true),
    RT_ENTRY(xrGetD3D12GraphicsRequirementsKHR, true),
#endif
    RT_ENTRY(xrCreateSession, true),
    RT_ENTRY(xrDestroySession, true),
    RT_ENTRY(xrBeginSession, true),
    RT_ENTRY(xrEndSession, true),
    RT_ENTRY(xrRequestExitSession, true),
    RT_ENTRY(xrEnumerateReferenceSpaces, true),
    RT_ENTRY(xrCreateReferenceSpace, true),
    RT_ENTRY(xrGetReferenceSpaceBoundsRect, true),
    RT_ENTRY(xrCreateActionSpace, true),
    RT_ENTRY(xrDestroySpace, true),
    RT_ENTRY(xrLocateSpace, true),
    RT_ENTRY(xrLocateSpaces, true),
    RT_ENTRY(xrEnumerateSwapchainFormats, true),
    RT_ENTRY(xrCreateSwapchain, true),
    RT_ENTRY(xrDestroySwapchain, true),
    RT_ENTRY(xrEnumerateSwapchainImages, true),
    RT_ENTRY(xrAcquireSwapchainImage, true),
    RT_ENTRY(xrWaitSwapchainImage, true),
    RT_ENTRY(xrReleaseSwapchainImage, true),
    RT_ENTRY(xrWaitFrame, true),
    RT_ENTRY(xrBeginFrame, true),
    RT_ENTRY(xrEndFrame, true),
    RT_ENTRY(xrLocateViews, true),
    RT_ENTRY(xrCreateActionSet, true),
    RT_ENTRY(xrDestroyActionSet, true),
    RT_ENTRY(xrCreateAction, true),
    RT_ENTRY(xrDestroyAction, true),
    RT_ENTRY(xrSuggestInteractionProfileBindings, true),
    RT_ENTRY(xrAttachSessionActionSets, true),
    RT_ENTRY(xrGetCurrentInteractionProfile, true),
    RT_ENTRY(xrGetActionStateBoolean, true),
    RT_ENTRY(xrGetActionStateFloat, true),
    RT_ENTRY(xrGetActionStateVector2f, true),
    RT_ENTRY(xrGetActionStatePose, true),
    RT_ENTRY(xrSyncActions, true),
    RT_ENTRY(xrEnumerateBoundSourcesForAction, true),
    RT_ENTRY(xrGetInputSourceLocalizedName, true),
    RT_ENTRY(xrApplyHapticFeedback, true),
    RT_ENTRY(xrStopHapticFeedback, true),
};

#undef RT_ENTRY

XrResult XRAPI_CALL rt_xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function) {
    if (!function || !name) {
        return XR_ERROR_VALIDATION_FAILURE;
    }
    *function = nullptr;
    if (std::strcmp(name, "xrGetInstanceProcAddr") == 0) {
        *function = reinterpret_cast<PFN_xrVoidFunction>(rt_xrGetInstanceProcAddr);
        return XR_SUCCESS;
    }
    for (const Entry& entry : kEntries) {
        if (std::strcmp(entry.name, name) == 0) {
            if (entry.needs_instance && !check_instance(instance)) {
                return XR_ERROR_HANDLE_INVALID;
            }
            *function = entry.function;
            return XR_SUCCESS;
        }
    }
    return XR_ERROR_FUNCTION_UNSUPPORTED;
}

const char* result_name(XrResult result) {
    switch (result) {
#define RT_CASE(value, number) \
    case value:                \
        return #value;
        XR_LIST_ENUM_XrResult(RT_CASE)
#undef RT_CASE
    default:
        return "XR_UNKNOWN_RESULT";
    }
}

const char* structure_name(XrStructureType type) {
    switch (type) {
#define RT_CASE(value, number) \
    case value:                \
        return #value;
        XR_LIST_ENUM_XrStructureType(RT_CASE)
#undef RT_CASE
    default:
        return "XR_UNKNOWN_STRUCTURE_TYPE";
    }
}

}  // namespace

#ifdef _WIN32
#define RUNTIME_EXPORT __declspec(dllexport)
#else
#define RUNTIME_EXPORT __attribute__((visibility("default")))
#endif

extern "C" RUNTIME_EXPORT XrResult XRAPI_CALL xrNegotiateLoaderRuntimeInterface(
    const XrNegotiateLoaderInfo* loaderInfo, XrNegotiateRuntimeRequest* runtimeRequest) {
    if (!loaderInfo || !runtimeRequest || loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
        loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo) ||
        runtimeRequest->structType != XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST ||
        runtimeRequest->structVersion != XR_RUNTIME_INFO_STRUCT_VERSION ||
        runtimeRequest->structSize != sizeof(XrNegotiateRuntimeRequest)) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    if (XR_CURRENT_LOADER_RUNTIME_VERSION < loaderInfo->minInterfaceVersion ||
        XR_CURRENT_LOADER_RUNTIME_VERSION > loaderInfo->maxInterfaceVersion ||
        XR_VERSION_MAJOR(loaderInfo->minApiVersion) > 1 || XR_VERSION_MAJOR(loaderInfo->maxApiVersion) < 1) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }
    runtimeRequest->runtimeInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
    runtimeRequest->runtimeApiVersion = XR_API_VERSION_1_1;
    runtimeRequest->getInstanceProcAddr = rt_xrGetInstanceProcAddr;
    return XR_SUCCESS;
}
