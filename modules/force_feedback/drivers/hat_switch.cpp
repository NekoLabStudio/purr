#include "hat_switch.h"

namespace {

constexpr std::uint32_t kGenericDesktopHatSwitch = 0x00010039;
constexpr int kUp = 1;
constexpr int kRight = 2;
constexpr int kDown = 4;
constexpr int kLeft = 8;
// Eight positions clockwise from up; a four-way hat takes every other one.
constexpr int kPositions[] = {kUp, kUp | kRight, kRight, kRight | kDown, kDown, kDown | kLeft, kLeft, kLeft | kUp};

}  // namespace

std::optional<HatSwitch> hat_switch(const ReportDescriptor& descriptor) {
    for (const HidField& field : descriptor.fields()) {
        if (field.type != ReportType::input || !field.variable || field.constant) {
            continue;
        }
        const int slot = field.slot_of(kGenericDesktopHatSwitch);
        if (slot < 0 || field.size == 0 || field.logical_max <= field.logical_min) {
            continue;
        }
        return HatSwitch{field.report_id, descriptor.numbered(), field.bit_offset + field.size * slot, field.size,
                         field.logical_min, field.logical_max};
    }
    return std::nullopt;
}

std::optional<int> read_hat(const HatSwitch& hat, const Bytes& report) {
    if (hat.numbered && (report.empty() || report[0] != hat.report_id)) {
        return std::nullopt;
    }
    if (report.size() * 8 < hat.bit_offset + hat.size) {
        return std::nullopt;
    }
    const std::int64_t raw = get_bits(report, hat.bit_offset, hat.size, hat.logical_min < 0);
    if (raw < hat.logical_min || raw > hat.logical_max) {
        return 0;  // its null state: centred
    }
    const std::int64_t index = raw - hat.logical_min;
    switch (hat.logical_max - hat.logical_min + 1) {
    case 8:
        return kPositions[index];
    case 4:
        return kPositions[index * 2];
    default:
        return 0;
    }
}
