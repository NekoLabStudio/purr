#pragma once

// Taking a wheel out of the mode it starts in, straight over USB:
// - Vendor control transfers on endpoint 0 (Thrustmaster's boot switch).
//   Neither needs the device opened, and both work as an ordinary user
//   (crossover-wheel's src/mac/bootswitch.c, measured on macOS 26), so the
//   HID driver keeps the device and the game keeps its steering.
// - A message on an interrupt or bulk OUT endpoint of a vendor class device
//   no driver has claimed (Logitech's wheels in Xbox mode), through its
//   interface's user client, opened without seizing it. If something else
//   holds the interface the open fails and that's logged.

#include "../drivers/wheel_driver.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace usb_device {

// Whether a USB device with these IDs is attached.
bool attached(std::uint16_t vendor, std::uint16_t product);
// The vendor and product IDs of a USB device's service.
std::pair<std::uint16_t, std::uint16_t> ids(io_service_t service);
// A dictionary matching USB devices with these IDs, for IOKit to take.
CFMutableDictionaryRef matching(std::uint16_t vendor, std::uint16_t product);
// A device-to-host vendor request's answer, or nothing if it failed.
std::optional<Bytes> request_in(std::uint16_t vendor, std::uint16_t product, std::uint8_t request_type,
                                std::uint8_t request, std::uint16_t length);
// A host-to-device vendor request with no data. True if it went, including
// when the device left the bus before answering, as a mode switch does.
bool request_out(std::uint16_t vendor, std::uint16_t product, std::uint8_t request_type, std::uint8_t request,
                 std::uint16_t value);
// `message` on OUT endpoint `endpoint` (its number) of the first interface
// that has it, the device configured first if nothing has. True if it went,
// including when the device left the bus as it took it.
bool write_endpoint(std::uint16_t vendor, std::uint16_t product, std::uint8_t endpoint, const Bytes& message);

}  // namespace usb_device
