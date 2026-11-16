#pragma once

// Where a wheel's hat switch (its D-pad) sits in its input reports, by its
// report descriptor: the first variable input field with Generic Desktop
// Hat Switch. Godot reports a hat's directions as joypad buttons 11 to 14,
// the same numbers as the wheel's own buttons 11 to 14 (a G923's shifter's
// first gears), so the game reads the hat here to tell them apart
// (SteeringWheelHats).

#include "report_descriptor.h"

#include <optional>

struct HatSwitch {
    std::uint8_t report_id = 0;
    bool numbered = false;
    std::uint32_t bit_offset = 0;
    std::uint32_t size = 0;
    std::int32_t logical_min = 0;
    std::int32_t logical_max = 0;
};

std::optional<HatSwitch> hat_switch(const ReportDescriptor& descriptor);
// The directions held, as Godot's hat mask (up 1, right 2, down 4, left 8;
// 0 centred), from one input report; nothing if the report isn't the hat's.
std::optional<int> read_hat(const HatSwitch& hat, const Bytes& report);
