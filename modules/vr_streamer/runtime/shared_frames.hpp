#pragma once

#include "platform.hpp"
#include "vr_shared.hpp"

#include <atomic>
#include <cstdint>
#include <string>

// The shared file the streamer reads (vr_shared.hpp): the runtime publishes each frame's eyes in
// its slots and reads the headset's pose and controllers from its header. The graphics backend
// copies the pixels (graphics.hpp); this holds the file and everything around them.
class SharedFrames {
public:
    struct View {
        float orientation[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float position[3] = {0.0f, 0.0f, 0.0f};
        float fov[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // left, right, up, down
    };
    // What goes beside a frame's pixels.
    struct Frame {
        std::uint32_t frame_id = 0;
        std::uint64_t predicted_display_time_ns = 0;
        std::uint64_t submitted_at_unix_ns = 0;
        std::uint64_t tracking_timestamp_ns = 0;
        View views[2];
    };

    // Maps the shared file for eyes of this size and format; false if it couldn't be made.
    bool start(std::uint32_t eye_width, std::uint32_t eye_height, vr_shared::PixelFormat format);
    // The graphics backend must have no copies in flight.
    void stop();

    bool started() const { return mapping_.view != nullptr; }
    const std::string& path() const { return path_; }
    // Where the shared file for this process goes, whether or not it exists yet.
    static std::string shared_path();

    // True while the streamer is reading; when false the caller skips the copy, so the game pays
    // nothing. `now_ns` is the monotonic clock.
    bool reader_present(std::uint64_t now_ns);
    void note_frame_submitted();

    bool read_host_state(vr_shared::HostState& out) const;
    bool read_input_state(vr_shared::InputState& out) const;
    // Asks the streamer to vibrate a hand's controller (0 left, 1 right).
    void write_haptic(int hand, float duration_s, float frequency, float amplitude);

    // Reserves a slot for a frame's pixels and marks it being written; -1 (and the frame
    // counted as dropped) if every slot is still being written.
    int begin_slot();
    // Where a slot's pixels go: both eyes, left then right, each eye_size() bytes in rows of
    // eye_stride() bytes. Also as an offset from the mapping's start.
    std::uint8_t* slot_pixels(int slot) const;
    std::uint64_t slot_pixels_offset(int slot) const;
    // Publishes a slot whose pixels are in (or, if not `ok`, gives it up).
    void publish(int slot, const Frame& frame, bool ok, std::uint32_t readback_us, std::uint32_t copy_us);
    // Whether any slot is still being written.
    bool busy() const;

    void* view() const { return mapping_.view; }
    std::size_t mapped_size() const { return mapping_.size; }
    std::uint32_t eye_width() const { return eye_width_; }
    std::uint32_t eye_height() const { return eye_height_; }
    std::uint32_t eye_stride() const { return eye_stride_; }
    std::uint32_t eye_size() const { return eye_size_; }

private:
    vr_shared::SlotHeader* slot_header(int slot) const;

    std::string path_;
    platform::Mapping mapping_;
    std::uint32_t eye_width_ = 0;
    std::uint32_t eye_height_ = 0;
    std::uint32_t eye_stride_ = 0;
    std::uint32_t eye_size_ = 0;
    std::uint32_t next_slot_ = 0;
    std::atomic<bool> writing_[vr_shared::kSlotCount] = {};
    std::uint32_t sequence_at_begin_[vr_shared::kSlotCount] = {};

    std::uint64_t last_heartbeat_ = 0;
    std::uint64_t last_heartbeat_change_ns_ = 0;
};
