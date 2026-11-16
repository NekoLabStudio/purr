#include "steering_axis.h"

#include <algorithm>

namespace {

constexpr std::uint32_t kGenericDesktopX = 0x00010030;

}  // namespace

std::optional<SteeringAxis> steering_axis(const ReportDescriptor& descriptor) {
    for (const HidField& field : descriptor.fields()) {
        if (field.type != ReportType::input || !field.variable || field.constant) {
            continue;
        }
        const int slot = field.slot_of(kGenericDesktopX);
        if (slot < 0 || field.size == 0 || field.logical_max <= field.logical_min) {
            continue;
        }
        return SteeringAxis{field.report_id, descriptor.numbered(), field.bit_offset + field.size * slot, field.size,
                            field.logical_min, field.logical_max};
    }
    return std::nullopt;
}

std::optional<double> read_steering(const SteeringAxis& axis, const Bytes& report) {
    if (axis.numbered && (report.empty() || report[0] != axis.report_id)) {
        return std::nullopt;
    }
    if (report.size() * 8 < axis.bit_offset + axis.size) {
        return std::nullopt;
    }
    const std::int64_t raw = get_bits(report, axis.bit_offset, axis.size, axis.logical_min < 0);
    const double span = double(axis.logical_max) - double(axis.logical_min);
    return std::clamp((double(raw) - double(axis.logical_min)) / span * 2.0 - 1.0, -1.0, 1.0);
}
