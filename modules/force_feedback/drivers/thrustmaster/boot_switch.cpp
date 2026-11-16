#include "boot_switch.h"

#include <algorithm>
#include <iterator>

namespace thrustmaster {

namespace {

struct Model {
    std::uint16_t wheel_type;
    std::uint16_t switch_value;
};
// hid-thrustmaster.c's tm_wheels_infos, less the T500RS (not driven here),
// plus the TMX: the kernel's commented-out row gives its type, tmdrv the
// value its second step takes at B65D.
constexpr Model kModels[] = {
    {0x0306, 0x0006},  // T150RS
    {0x0200, 0x0005},  // T300RS, attachment missing
    {0x0206, 0x0005},  // T300RS
    {0x0209, 0x0005},  // T300RS, open wheel attachment
    {0x020A, 0x0005},  // T300RS, Sparco R383 mod
    {0x0204, 0x0005},  // T300 Ferrari Alcantara Edition
    {0x0407, 0x0007},  // TMX
};

}  // namespace

bool is_boot(std::uint16_t product) {
    return std::find(std::begin(kBootProducts), std::end(kBootProducts), product) != std::end(kBootProducts);
}

std::optional<std::uint16_t> first_step(std::uint16_t product) {
    switch (product) {
    case kTmxBoot:
        return 0x0001;  // tmdrv's TMX step 1: comes back at B65D
    case kTxBoot:
        return 0x0001;  // tmdrv's TX step 1: comes back at B65D
    case kTsxwBoot:
        return 0x000A;  // tmdrv's TS-XW: comes back at B692
    default:
        return std::nullopt;
    }
}

std::uint16_t wheel_type(const Bytes& model_reply) {
    if (model_reply.size() < 8) {
        return 0;
    }
    return static_cast<std::uint16_t>(model_reply[6] | (model_reply[7] << 8));
}

std::optional<std::uint16_t> switch_at_boot(const Bytes& model_reply, std::uint16_t came_from) {
    const std::uint16_t type = wheel_type(model_reply);
    for (const Model& model : kModels) {
        if (model.wheel_type == type) {
            return model.switch_value;
        }
    }
    // tmdrv's second steps, for a wheel that gave no model it knows.
    switch (came_from) {
    case kTmxBoot:
        return 0x0007;
    case kTxBoot:
        return 0x0004;
    default:
        return std::nullopt;
    }
}

}  // namespace thrustmaster
