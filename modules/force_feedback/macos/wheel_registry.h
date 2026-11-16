#pragma once

// Which driver plays a device: known wheels by vendor and product ID first
// (Logitech, Thrustmaster, Fanatec, Simagic), then any device whose report
// descriptor carries HID PID. Known IDs win over the descriptor. Also which
// devices are a wheel in a starting mode, to be switched to their own
// before they can be driven, which wheel to prefer when the game names
// one, and which of a wheel's other interfaces a driver needs. No IOKit
// here, so the tests cover it.

#include "../known_wheels.h"
#include "../drivers/report_descriptor.h"
#include "../drivers/thrustmaster/t300.h"
#include "../drivers/wheel_driver.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace wheel_registry {



// What IOKit says about one HID device (one interface of a wheel).
struct Device {
    std::uint16_t vendor = 0;
    std::uint16_t product = 0;
    // bcdDevice, the release number.
    std::uint16_t version = 0;
    // The USB port it's on: every interface of one wheel shares it.
    std::uint32_t location = 0;
    std::string product_name;
    std::size_t max_output_report = 0;
    ReportDescriptor descriptor;
};

// What to do with a device.
struct Choice {
    enum class Kind { none, drive, switch_logitech, switch_thrustmaster } kind = Kind::none;
    Family family = Family::pid;
    std::string name;
    bool verified = false;
    const KnownWheel* known = nullptr;
};

// Every vendor and product ID a wheel driven or switched here shows up as
// over HID, for matching devices. Only these: matching all of a maker's
// devices would take in its keyboards and mice, which macOS guards.
std::vector<std::pair<std::uint16_t, std::uint16_t>> products();
Choice choose(const Device& device);

// The order to try devices in: those with the game's wheel's vendor and
// product IDs first (when it names one: not 0), then the rest as they are.
std::vector<std::size_t> preferred_order(const std::vector<Device>& devices, std::uint16_t vendor,
                                         std::uint16_t product);

// Logitech's wheels in Xbox mode (the G920 and the G923 for Xbox) start as
// a vendor class USB device, not HID, and are switched by a message on an
// interrupt endpoint (usb_modeswitch's entries for them), after which they
// come back as HID++ wheels.
struct UsbSwitch {
    std::uint8_t endpoint = 0;
    Bytes message;
    std::uint16_t native_product = 0;
};
std::optional<UsbSwitch> usb_switch(std::uint16_t vendor, std::uint16_t product);
// The USB vendor and product IDs those start as, for watching.
std::vector<std::pair<std::uint16_t, std::uint16_t>> usb_switch_products();

// The other interfaces of a wheel that a driver needs besides the one it
// plays forces on (the direct-drive Logitechs').
enum class Sibling { none, hidpp, input };
bool needs_siblings(Family family);
// What another interface of the same wheel is to its driver.
Sibling sibling(const Device& device);
struct Siblings {
    Transport* hidpp = nullptr;
    Transport* input = nullptr;
};

// The driver for a device `choose` said to drive.
std::unique_ptr<WheelDriver> make_driver(const Choice& choice, const Device& device, Transport& transport,
                                         const Siblings& siblings = {});

}  // namespace wheel_registry
