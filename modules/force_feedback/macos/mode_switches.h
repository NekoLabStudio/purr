#pragma once

// Taking wheels out of the modes they start in: Logitech's older wheels in
// Driving Force mode and the G923 in PlayStation mode (lg4ff), Logitech's in
// Xbox mode (a vendor class USB device until switched), and Thrustmaster's
// at their boot IDs. A switched wheel leaves the bus and comes back under
// its own product ID and name, so it's switched as soon as it's plugged in
// (watch), before the game binds it: a binding names the device as it was,
// and a wheel switched after it lost both its steering and its forces.
// Also whenever the game asks (prepare) and as the output looks for a wheel
// to open. Process-wide, so a wheel is switched once, not by each looker.

#include "hid_device.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>

class ModeSwitches {
public:
    static ModeSwitches& shared();

    // Switches the device if it's in a starting mode; whether it was in one.
    bool take(FoundDevice& device);
    // Every wheel attached now in a starting mode.
    void prepare(HidManager& devices);
    // From now on, each wheel as it's plugged in (and those there already),
    // on the main run loop. Once a process; it's never undone.
    void watch();

private:
    using Clock = std::chrono::steady_clock;

    // Whether a switch went to this product lately; if not, it's now.
    bool switched_lately(std::uint16_t vendor, std::uint16_t product);
    void switch_logitech(FoundDevice& found);
    void switch_thrustmaster(std::uint16_t product);
    void switch_usb(std::uint16_t vendor, std::uint16_t product);
    static void on_usb_matched(void* context, io_iterator_t services);

    std::recursive_mutex mutex_;
    std::map<std::uint32_t, Clock::time_point> switched_;
    std::uint16_t first_step_from_ = 0;
    Clock::time_point first_step_at_{};
    std::unique_ptr<HidManager> watching_;
    bool watched_ = false;
};
