#include "wheel_registry.h"

#include "../drivers/fanatec/fanatec.h"
#include "../drivers/hid_pid/pid.h"
#include "../drivers/lg4ff/lg4ff.h"
#include "../drivers/logitech_hidpp/hidpp.h"
#include "../drivers/logitech_stream/logitech_stream.h"
#include "../drivers/simagic/simagic.h"
#include "../drivers/thrustmaster/boot_switch.h"
#include "../drivers/thrustmaster/t150.h"

#include <algorithm>

namespace wheel_registry {

namespace {

constexpr std::uint16_t kPidPage = 0x0F;
constexpr std::uint16_t kGenericDesktopPage = 0x01;
// HID++'s short report, which the direct-drive Logitechs' HID++ interface takes.
constexpr std::uint8_t kHidppShortReport = 0x10;


// usb_modeswitch's entries for the G920 and the G923 for Xbox
// (MessageEndpoint 0x01, MessageContent 0f00010142, TargetClass 0x03).
struct XboxMode {
    std::uint16_t product;
    std::uint16_t native_product;
};
constexpr XboxMode kXboxModes[] = {{0xC261, 0xC262}, {0xC26D, 0xC26E}};
constexpr std::uint8_t kXboxSwitchEndpoint = 0x01;

bool has_output_report(const ReportDescriptor& descriptor, std::uint8_t id) {
    const std::vector<std::uint8_t> ids = descriptor.report_ids(ReportType::output);
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

bool has_reports(const ReportDescriptor& descriptor, ReportType type) {
    for (const HidField& field : descriptor.fields()) {
        if (field.type == type) {
            return true;
        }
    }
    return false;
}

// Whether this interface of a known wheel is the one that takes its forces.
bool takes_forces(const KnownWheel& wheel, const Device& device) {
    switch (wheel.family) {
    case Family::hidpp:
        return has_output_report(device.descriptor, hidpp::kLongReport);
    case Family::fanatec:
    case Family::simagic:
        return !device.descriptor.report_ids(ReportType::output).empty();
    case Family::logitech_stream:
        return has_output_report(device.descriptor, logitech_stream::kForceReport) &&
               !has_output_report(device.descriptor, hidpp::kLongReport);
    case Family::lg4ff:
        // Its forces go to the wheel's own joystick interface. A G923 also
        // shows vendor interfaces with output reports (G HUB's), which take
        // the commands and ignore them: macOS lists them in no set order,
        // and one run drove that and nothing was felt. A descriptor that
        // couldn't be read says nothing either way.
        return device.max_output_report > 0 &&
               (device.descriptor.fields().empty() || device.descriptor.uses_page(kGenericDesktopPage));
    default:
        return device.max_output_report > 0;
    }
}

}  // namespace


std::vector<std::pair<std::uint16_t, std::uint16_t>> products() {
    std::vector<std::pair<std::uint16_t, std::uint16_t>> found;
    for (const KnownWheel& wheel : known_wheels()) {
        found.emplace_back(wheel.vendor, wheel.product);
    }
    for (const std::uint16_t product : thrustmaster::kBootProducts) {
        found.emplace_back(kThrustmaster, product);
    }
    return found;
}

Choice choose(const Device& device) {
    Choice choice;
    if (device.vendor == kThrustmaster && thrustmaster::is_boot(device.product)) {
        choice.kind = Choice::Kind::switch_thrustmaster;
        choice.name = "Thrustmaster wheel";
        return choice;
    }
    if (device.vendor == kLogitech && device.max_output_report > 0) {
        if (const std::optional<lg4ff::ModeSwitch> to = lg4ff::native_mode_switch(device.product, device.version)) {
            const KnownWheel* native = known_wheel(kLogitech, to->native_product);
            choice.kind = Choice::Kind::switch_logitech;
            choice.name = native ? native->name : "Logitech wheel";
            choice.verified = native && native->verified;
            return choice;
        }
    }
    const bool pid = device.descriptor.uses_page(kPidPage) && !device.descriptor.report_ids(ReportType::output).empty();
    if (const KnownWheel* wheel = known_wheel(device.vendor, device.product)) {
        // A Simagic base on firmware up to v159 still speaks PID.
        if (wheel->family == Family::simagic && pid) {
            choice.kind = Choice::Kind::drive;
            choice.family = Family::pid;
            choice.name = wheel->name;
            return choice;
        }
        if (takes_forces(*wheel, device)) {
            choice.kind = Choice::Kind::drive;
            choice.family = wheel->family;
            choice.name = wheel->name;
            choice.verified = wheel->verified;
            choice.known = wheel;
        }
        return choice;
    }
    if (pid) {
        choice.kind = Choice::Kind::drive;
        choice.family = Family::pid;
        choice.name = device.product_name.empty() ? "Force feedback wheel" : device.product_name;
    }
    return choice;
}

std::vector<std::size_t> preferred_order(const std::vector<Device>& devices, std::uint16_t vendor,
                                         std::uint16_t product) {
    std::vector<std::size_t> order;
    const bool named = vendor != 0 || product != 0;
    for (std::size_t i = 0; i < devices.size(); ++i) {
        if (named && devices[i].vendor == vendor && devices[i].product == product) {
            order.push_back(i);
        }
    }
    for (std::size_t i = 0; i < devices.size(); ++i) {
        if (std::find(order.begin(), order.end(), i) == order.end()) {
            order.push_back(i);
        }
    }
    return order;
}

std::optional<UsbSwitch> usb_switch(std::uint16_t vendor, std::uint16_t product) {
    if (vendor != kLogitech) {
        return std::nullopt;
    }
    for (const XboxMode& mode : kXboxModes) {
        if (mode.product == product) {
            return UsbSwitch{kXboxSwitchEndpoint, {0x0F, 0x00, 0x01, 0x01, 0x42}, mode.native_product};
        }
    }
    return std::nullopt;
}

std::vector<std::pair<std::uint16_t, std::uint16_t>> usb_switch_products() {
    std::vector<std::pair<std::uint16_t, std::uint16_t>> found;
    for (const XboxMode& mode : kXboxModes) {
        found.emplace_back(kLogitech, mode.product);
    }
    return found;
}

bool needs_siblings(Family family) {
    return family == Family::logitech_stream;
}

Sibling sibling(const Device& device) {
    if (has_output_report(device.descriptor, kHidppShortReport) ||
        has_output_report(device.descriptor, hidpp::kLongReport)) {
        return Sibling::hidpp;
    }
    if (!has_reports(device.descriptor, ReportType::output) && has_reports(device.descriptor, ReportType::input)) {
        return Sibling::input;
    }
    return Sibling::none;
}

std::unique_ptr<WheelDriver> make_driver(const Choice& choice, const Device& device, Transport& transport,
                                         const Siblings& siblings) {
    const auto first_output = [&device] {
        const std::vector<std::uint8_t> ids = device.descriptor.report_ids(ReportType::output);
        return ids.empty() ? std::uint8_t{0} : ids.front();
    };
    switch (choice.family) {
    case Family::lg4ff:
        return std::make_unique<Lg4ffDriver>(transport, device.product,
                                             lg4ff::is_formula_force_ex(device.product, device.version));
    case Family::hidpp:
        return std::make_unique<HidppDriver>(
            transport, device.descriptor.length(ReportType::output, hidpp::kVeryLongReport), device.version);
    case Family::t150:
        return std::make_unique<T150Driver>(transport, choice.known->range);
    case Family::t300:
        return std::make_unique<T300Driver>(transport, choice.known->buffer, choice.known->opening,
                                            device.max_output_report, choice.known->range_least, choice.known->range);
    case Family::fanatec: {
        const std::uint8_t id = first_output();
        return std::make_unique<FanatecDriver>(transport, id, device.descriptor.length(ReportType::output, id),
                                               choice.known->high_resolution, choice.known->range);
    }
    case Family::pid:
        return std::make_unique<PidDriver>(transport, device.descriptor);
    case Family::simagic: {
        const std::uint8_t id = first_output();
        return std::make_unique<SimagicDriver>(transport, id, device.descriptor.length(ReportType::output, id));
    }
    case Family::logitech_stream:
        return std::make_unique<LogitechStreamDriver>(transport, siblings.hidpp, siblings.input);
    }
    return nullptr;
}

}  // namespace wheel_registry
