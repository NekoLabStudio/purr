#pragma once

// SteeringWheelHats: a wheel's hat switch (its D-pad), for the game to tell its
// directions from the wheel's own buttons with the same numbers (Godot
// reports a hat as joypad buttons 11 to 14: scripts/controls.gd), and on
// Windows its own buttons by their own numbers (SDL can read a wheel as an
// Xbox gamepad and renumber them). Listens only, so it's safe beside force
// feedback and in the menus.

#include "hat_reader.h"

#include "core/object/ref_counted.h"
#include "core/variant/variant.h"

#include <memory>

class SteeringWheelHats : public RefCounted {
    GDCLASS(SteeringWheelHats, RefCounted)

public:
    // Starts reading the hat of the wheel with these USB IDs; whether it has one to read.
    bool watch(int64_t vendor_id, int64_t product_id);
    // The directions held (up 1, right 2, down 4, left 8; 0 centred), or -1
    // when there's no hat being read or none heard yet.
    int64_t get_hat();
    // Whether the wheel's own buttons are read here (Windows), and those held
    // now by their own numbers (0 the first).
    bool reads_buttons();
    PackedInt32Array get_buttons();
    void close();

protected:
    static void _bind_methods();

private:
    std::unique_ptr<HatReader> reader_;
};
