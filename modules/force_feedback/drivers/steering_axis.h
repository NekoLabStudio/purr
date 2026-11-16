#pragma once

// Where a wheel's steering sits in its input reports, by its report
// descriptor: the first variable input field with Generic Desktop X, as
// joypad input (SDL, which the game reads it through) takes it. Read at the
// force feedback's own rate, the steering's damping works on a fresh
// position rather than the game's, a frame or two old.

#include "report_descriptor.h"

#include <optional>

struct SteeringAxis {
    std::uint8_t report_id = 0;
    bool numbered = false;
    std::uint32_t bit_offset = 0;
    std::uint32_t size = 0;
    std::int32_t logical_min = 0;
    std::int32_t logical_max = 0;
};

std::optional<SteeringAxis> steering_axis(const ReportDescriptor& descriptor);
// The axis from one input report, -1 to 1 over its logical range; nothing
// if the report isn't the one it's in.
std::optional<double> read_steering(const SteeringAxis& axis, const Bytes& report);
