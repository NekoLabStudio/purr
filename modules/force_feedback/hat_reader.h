#pragma once

// A wheel's hat switch (its D-pad), and on Windows its buttons, read from
// its own input reports as they come, listening only: nothing is ever sent
// to the wheel, so it reads the same alongside force feedback and the game's
// own joypad input. One per platform (macOS: the wheel's HID device;
// Windows: DirectInput's POV and buttons).

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class HatReader {
public:
    virtual ~HatReader() = default;
    // The directions held now, as Godot's hat mask (up 1, right 2, down 4,
    // left 8; 0 centred); nothing until a report with the hat has come.
    virtual std::optional<int> hat() = 0;
    // The wheel's own buttons held now, by their own numbers (0 the first),
    // where this platform's joypad input may number them otherwise: on
    // Windows SDL can read a wheel as an Xbox gamepad and give it that
    // layout, where its right stick button and a shifter gear came as one.
    // Nothing where the joypads already come by their own numbers (macOS).
    virtual std::optional<std::vector<int>> buttons() { return std::nullopt; }
    // Whether it reads the wheel's buttons at all (a read can fail for a moment).
    virtual bool reads_buttons() const { return false; }

    // The reader for the wheel with these USB IDs that has a hat, or null
    // (no such wheel, or no hat). window: the game's (Windows asks for one).
    static std::unique_ptr<HatReader> open(std::uint16_t vendor_id, std::uint16_t product_id, std::uintptr_t window);
};
