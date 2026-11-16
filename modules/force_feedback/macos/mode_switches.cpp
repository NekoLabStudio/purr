#include "mode_switches.h"

#include "../drivers/lg4ff/lg4ff.h"
#include "../drivers/thrustmaster/boot_switch.h"
#include "../wheel_log.h"
#include "usb_device.h"
#include "wheel_registry.h"

#include <IOKit/IOKitLib.h>

namespace {

using wheel_registry::Choice;

// A switch isn't sent again to the same product sooner than this: the
// wheel takes a moment to leave and come back.
constexpr auto kSwitchAgain = std::chrono::seconds(10);
// How long a two-step Thrustmaster switch remembers its first step.
constexpr auto kSecondStep = std::chrono::seconds(30);

}  // namespace

ModeSwitches& ModeSwitches::shared() {
    // Never destroyed: it's watching on the main run loop until the process
    // ends, and taking it down as statics go could run under a run loop
    // already gone.
    static ModeSwitches* switches = new ModeSwitches;
    return *switches;
}

bool ModeSwitches::take(FoundDevice& device) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    switch (wheel_registry::choose(device.facts).kind) {
    case Choice::Kind::switch_logitech:
        switch_logitech(device);
        return true;
    case Choice::Kind::switch_thrustmaster:
        switch_thrustmaster(device.facts.product);
        return true;
    default:
        return false;
    }
}

void ModeSwitches::prepare(HidManager& devices) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (FoundDevice& device : devices.devices()) {
        take(device);
    }
    for (const auto& [vendor, product] : wheel_registry::usb_switch_products()) {
        if (usb_device::attached(vendor, product)) {
            switch_usb(vendor, product);
        }
    }
}

void ModeSwitches::watch() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (watched_) {
        return;
    }
    watched_ = true;
    // The known wheels only, not every controller: nothing else switches.
    watching_ = std::make_unique<HidManager>(false, [this](FoundDevice& device) { take(device); });
    IONotificationPortRef port = IONotificationPortCreate(MACH_PORT_NULL);
    if (!port) {
        wheel_log("IONotificationPortCreate failed, so wheels in Xbox mode switch only once the game looks for one");
        return;
    }
    CFRunLoopAddSource(CFRunLoopGetMain(), IONotificationPortGetRunLoopSource(port), kCFRunLoopDefaultMode);
    for (const auto& [vendor, product] : wheel_registry::usb_switch_products()) {
        io_iterator_t arrivals = IO_OBJECT_NULL;
        if (IOServiceAddMatchingNotification(port, kIOFirstMatchNotification, usb_device::matching(vendor, product),
                                             &ModeSwitches::on_usb_matched, this, &arrivals) == KERN_SUCCESS) {
            on_usb_matched(this, arrivals);  // those there already; this also arms it
        }
    }
}

void ModeSwitches::on_usb_matched(void* context, io_iterator_t services) {
    auto* self = static_cast<ModeSwitches*>(context);
    while (const io_service_t service = IOIteratorNext(services)) {
        const auto [vendor, product] = usb_device::ids(service);
        IOObjectRelease(service);
        std::lock_guard<std::recursive_mutex> lock(self->mutex_);
        self->switch_usb(vendor, product);
    }
}

bool ModeSwitches::switched_lately(std::uint16_t vendor, std::uint16_t product) {
    const auto key = (static_cast<std::uint32_t>(vendor) << 16) | product;
    const auto now = Clock::now();
    const auto last = switched_.find(key);
    if (last != switched_.end() && now - last->second < kSwitchAgain) {
        return true;
    }
    switched_[key] = now;
    return false;
}

void ModeSwitches::switch_logitech(FoundDevice& found) {
    const wheel_registry::Device& facts = found.facts;
    if (switched_lately(facts.vendor, facts.product)) {
        return;
    }
    const std::optional<lg4ff::ModeSwitch> to = lg4ff::native_mode_switch(facts.product, facts.version);
    HidDevice device(found.service);
    bool sent = to && device.open(false, 0);
    if (sent) {
        for (const Bytes& command : to->commands) {
            sent = device.send(ReportType::output, to->report_id, command) && sent;
        }
    }
    wheel_log("switching " + usb_id(facts.vendor, facts.product) + " to its own mode (" +
              usb_id(facts.vendor, to ? to->native_product : 0) + ")" + (sent ? "" : ": the switch failed"));
}

void ModeSwitches::switch_thrustmaster(std::uint16_t product) {
    if (switched_lately(thrustmaster::kVendor, product)) {
        return;
    }
    std::optional<std::uint16_t> value = thrustmaster::first_step(product);
    if (product == thrustmaster::kBoot) {
        const std::optional<Bytes> model =
            usb_device::request_in(thrustmaster::kVendor, product, thrustmaster::kModelRequestType,
                                   thrustmaster::kModelRequest, thrustmaster::kModelLength);
        const bool recent = first_step_from_ != 0 && Clock::now() - first_step_at_ < kSecondStep;
        value = thrustmaster::switch_at_boot(model.value_or(Bytes{}), recent ? first_step_from_ : 0);
        if (!value) {
            wheel_log("a Thrustmaster wheel starting up (" + usb_id(thrustmaster::kVendor, product) +
                      ") answered model " + hex4(thrustmaster::wheel_type(model.value_or(Bytes{}))) +
                      ", which isn't known; not switched");
            return;
        }
    } else {
        first_step_from_ = product;
        first_step_at_ = Clock::now();
    }
    const bool sent = usb_device::request_out(thrustmaster::kVendor, product, thrustmaster::kSwitchRequestType,
                                              thrustmaster::kSwitchRequest, *value);
    wheel_log("switching " + usb_id(thrustmaster::kVendor, product) + " to its own mode with " + hex4(*value) +
              (sent ? "" : ": the switch failed"));
}

void ModeSwitches::switch_usb(std::uint16_t vendor, std::uint16_t product) {
    const std::optional<wheel_registry::UsbSwitch> to = wheel_registry::usb_switch(vendor, product);
    if (!to || switched_lately(vendor, product)) {
        return;
    }
    const bool sent = usb_device::write_endpoint(vendor, product, to->endpoint, to->message);
    wheel_log("switching " + usb_id(vendor, product) + " to its own mode (" + usb_id(vendor, to->native_product) +
              ")" + (sent ? "" : ": the switch failed"));
}
