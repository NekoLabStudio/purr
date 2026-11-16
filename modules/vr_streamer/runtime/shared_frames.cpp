#include "shared_frames.hpp"

#include "runtime_log.hpp"

#include <cstring>
#include <thread>

std::string SharedFrames::shared_path() {
    return platform::temporary_directory() + "purr_vr_frames_" + std::to_string(platform::process_id()) + ".bin";
}

bool SharedFrames::start(std::uint32_t eye_width, std::uint32_t eye_height, vr_shared::PixelFormat format) {
    if (started()) {
        return true;
    }
    if (format == vr_shared::PixelFormat::unknown) {
        runtime_log("frame export: unsupported swapchain format");
        return false;
    }
    if (eye_width == 0 || eye_height == 0 || eye_width > vr_shared::kMaxEyeWidth || eye_height > vr_shared::kMaxEyeHeight) {
        runtime_logf("frame export: refusing eye size %ux%u", eye_width, eye_height);
        return false;
    }
    eye_width_ = eye_width;
    eye_height_ = eye_height;
    eye_stride_ = eye_width * 4;
    eye_size_ = eye_stride_ * eye_height;

    // Whole pages: a GPU may write into the mapping through a buffer that must cover them.
    const std::uint64_t total = vr_shared::total_size(eye_size_);
    constexpr std::uint64_t kPage = 16384;
    path_ = shared_path();
    if (!platform::map_shared_file(path_, static_cast<std::size_t>((total + kPage - 1) / kPage * kPage), mapping_)) {
        runtime_logf("frame export: cannot map %s", path_.c_str());
        return false;
    }

    auto* header = static_cast<vr_shared::SharedHeader*>(mapping_.view);
    std::memset(mapping_.view, 0, vr_shared::kFirstSlotOffset);
    header->slot_count = vr_shared::kSlotCount;
    header->format = static_cast<std::uint32_t>(format);
    header->eye_width = eye_width;
    header->eye_height = eye_height;
    header->eye_stride_bytes = eye_stride_;
    header->eye_size_bytes = eye_size_;
    header->slot_stride_bytes = vr_shared::slot_stride(eye_size_);
    header->first_slot_offset = vr_shared::kFirstSlotOffset;
    header->total_size_bytes = total;
    header->latest_slot.store(-1, std::memory_order_relaxed);
    header->version = vr_shared::kVersion;
    // Magic last: the reader takes it as the sign the rest is valid.
    std::atomic_thread_fence(std::memory_order_release);
    header->magic = vr_shared::kMagic;
    runtime_logf("frame export: %ux%u per eye in %s", eye_width, eye_height, path_.c_str());
    return true;
}

void SharedFrames::stop() {
    if (!started()) {
        return;
    }
    static_cast<vr_shared::SharedHeader*>(mapping_.view)->magic = 0;
    platform::unmap_shared_file(mapping_, path_);
    path_.clear();
}

bool SharedFrames::reader_present(std::uint64_t now_ns) {
    if (!started()) {
        return false;
    }
    const auto* header = static_cast<const vr_shared::SharedHeader*>(mapping_.view);
    const std::uint64_t heartbeat = header->reader_heartbeat.load(std::memory_order_acquire);
    if (heartbeat != last_heartbeat_) {
        last_heartbeat_ = heartbeat;
        last_heartbeat_change_ns_ = now_ns;
    }
    return last_heartbeat_change_ns_ != 0 && (now_ns - last_heartbeat_change_ns_) < vr_shared::kReaderTimeoutNs;
}

void SharedFrames::note_frame_submitted() {
    if (started()) {
        static_cast<vr_shared::SharedHeader*>(mapping_.view)->frames_submitted.fetch_add(1, std::memory_order_relaxed);
    }
}

namespace {

// Copies a seqlocked block written by the other process; false if it was never written or kept
// changing.
template <typename T>
bool read_seqlocked(const void* view, std::uint64_t offset, T& out) {
    if (!view) {
        return false;
    }
    const auto* block = reinterpret_cast<const T*>(static_cast<const std::uint8_t*>(view) + offset);
    for (int attempt = 0; attempt < 4; ++attempt) {
        const std::uint32_t before = block->sequence.load(std::memory_order_acquire);
        if (before == 0) {
            return false;  // never written
        }
        if (before % 2 != 0) {
            continue;
        }
        // A byte copy of a struct holding an atomic: the seqlock, not the atomic, makes this
        // read consistent.
        std::memcpy(static_cast<void*>(&out), static_cast<const void*>(block), sizeof(out));
        std::atomic_thread_fence(std::memory_order_acquire);
        if (block->sequence.load(std::memory_order_acquire) == before) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool SharedFrames::read_host_state(vr_shared::HostState& out) const {
    return read_seqlocked(mapping_.view, vr_shared::kHostStateOffset, out);
}

bool SharedFrames::read_input_state(vr_shared::InputState& out) const {
    return read_seqlocked(mapping_.view, vr_shared::kInputStateOffset, out);
}

void SharedFrames::write_haptic(int hand, float duration_s, float frequency, float amplitude) {
    if (!started() || hand < 0 || hand > 1) {
        return;
    }
    auto* state = reinterpret_cast<vr_shared::HapticState*>(static_cast<std::uint8_t*>(mapping_.view) + vr_shared::kHapticStateOffset);
    const std::uint32_t before = state->sequence.load(std::memory_order_relaxed);
    state->sequence.store(before + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
    vr_shared::HapticPulse& pulse = state->hands[hand];
    pulse.serial += 1;
    pulse.duration_s = duration_s;
    pulse.frequency = frequency;
    pulse.amplitude = amplitude;
    std::atomic_thread_fence(std::memory_order_release);
    state->sequence.store(before + 2, std::memory_order_release);
}

vr_shared::SlotHeader* SharedFrames::slot_header(int slot) const {
    const auto* header = static_cast<const vr_shared::SharedHeader*>(mapping_.view);
    return reinterpret_cast<vr_shared::SlotHeader*>(static_cast<std::uint8_t*>(mapping_.view) + vr_shared::kFirstSlotOffset +
                                                     slot * header->slot_stride_bytes);
}

int SharedFrames::begin_slot() {
    if (!started()) {
        return -1;
    }
    auto* header = static_cast<vr_shared::SharedHeader*>(mapping_.view);
    const std::uint32_t slot = next_slot_;
    bool idle = false;
    if (!writing_[slot].compare_exchange_strong(idle, true)) {
        header->frames_dropped.fetch_add(1, std::memory_order_relaxed);
        return -1;
    }
    next_slot_ = (next_slot_ + 1) % vr_shared::kSlotCount;
    vr_shared::SlotHeader* slot_header_ = slot_header(static_cast<int>(slot));
    sequence_at_begin_[slot] = slot_header_->sequence.load(std::memory_order_relaxed);
    slot_header_->sequence.store(sequence_at_begin_[slot] + 1, std::memory_order_release);  // being written
    return static_cast<int>(slot);
}

std::uint64_t SharedFrames::slot_pixels_offset(int slot) const {
    const auto* header = static_cast<const vr_shared::SharedHeader*>(mapping_.view);
    return vr_shared::kFirstSlotOffset + slot * header->slot_stride_bytes + sizeof(vr_shared::SlotHeader);
}

std::uint8_t* SharedFrames::slot_pixels(int slot) const {
    return static_cast<std::uint8_t*>(mapping_.view) + slot_pixels_offset(slot);
}

void SharedFrames::publish(int slot, const Frame& frame, bool ok, std::uint32_t readback_us, std::uint32_t copy_us) {
    auto* header = static_cast<vr_shared::SharedHeader*>(mapping_.view);
    vr_shared::SlotHeader* out = slot_header(slot);
    const std::uint32_t sequence = sequence_at_begin_[slot];
    if (ok) {
        out->frame_id = frame.frame_id;
        out->predicted_display_time_ns = frame.predicted_display_time_ns;
        out->submitted_at_unix_ns = frame.submitted_at_unix_ns;
        out->published_at_unix_ns = platform::unix_ns();
        out->readback_us = readback_us;
        out->copy_us = copy_us;
        for (int eye = 0; eye < 2; ++eye) {
            const View& view = frame.views[eye];
            out->views[eye] = vr_shared::ViewPose{view.orientation[0], view.orientation[1], view.orientation[2], view.orientation[3],
                                                  view.position[0],    view.position[1],    view.position[2],    view.fov[0],
                                                  view.fov[1],         view.fov[2],         view.fov[3],         0.0f};
        }
        out->tracking_timestamp_ns = frame.tracking_timestamp_ns;
        std::atomic_thread_fence(std::memory_order_release);
        out->sequence.store(sequence + 2, std::memory_order_release);
        header->latest_slot.store(slot, std::memory_order_release);
        header->frames_published.fetch_add(1, std::memory_order_relaxed);
    } else {
        // Stable again, but not the latest: the reader never looks at it.
        out->sequence.store(sequence + 2, std::memory_order_release);
        header->frames_dropped.fetch_add(1, std::memory_order_relaxed);
    }
    writing_[slot].store(false, std::memory_order_release);
}

bool SharedFrames::busy() const {
    for (const std::atomic<bool>& writing : writing_) {
        if (writing.load(std::memory_order_acquire)) {
            return true;
        }
    }
    return false;
}
