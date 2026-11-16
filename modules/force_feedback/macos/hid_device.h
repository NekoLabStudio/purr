#pragma once

// IOKit HID on macOS: finding the devices that might be wheels, and one
// device opened as a driver's Transport. Devices are opened without seizing
// them: the game reads the wheel's axes through Godot from the same device,
// and a driver that goes wrong may lose force feedback but must never take
// the steering away.

#include "../drivers/wheel_driver.h"
#include "wheel_registry.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <dispatch/dispatch.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

// One HID device found, with what the registry needs to know of it.
struct FoundDevice {
    wheel_registry::Device facts;
    std::size_t max_input_report = 0;
    std::size_t max_feature_report = 0;
    // Its report descriptor as it came, for the log.
    Bytes descriptor;
    // The IOKit service, kept for opening a device object of our own.
    io_service_t service = IO_OBJECT_NULL;
};

FoundDevice describe(IOHIDDeviceRef device);

class HidManager {
public:
    // The known wheels' devices (by vendor and product), and with
    // `controllers` every joystick, gamepad and multi-axis controller (where
    // HID PID wheels are). `arrived`, if given, hears of each device as it's
    // attached (and those there already), on the main run loop.
    explicit HidManager(bool controllers = true, std::function<void(FoundDevice&)> arrived = nullptr);
    ~HidManager();
    HidManager(const HidManager&) = delete;
    HidManager& operator=(const HidManager&) = delete;

    // As attached now.
    std::vector<FoundDevice> devices();

private:
    static void on_matched(void* context, IOReturn result, void* sender, IOHIDDeviceRef device);

    IOHIDManagerRef manager_ = nullptr;
    std::function<void(FoundDevice&)> arrived_;
};

class HidDevice : public Transport {
public:
    explicit HidDevice(io_service_t service);
    ~HidDevice() override;
    HidDevice(const HidDevice&) = delete;
    HidDevice& operator=(const HidDevice&) = delete;

    // `listen` collects the input reports it sends, for next_input.
    bool open(bool listen, std::size_t max_input_report);

    bool send(ReportType type, std::uint8_t id, const Bytes& report) override;
    std::optional<Bytes> read_feature(std::uint8_t id, std::size_t length) override;
    std::optional<Bytes> next_input(std::chrono::milliseconds timeout) override;

    // The newest input report, while listening (report ID first when numbered).
    std::optional<Bytes> latest_input();

    // Reports sent since it opened.
    std::uint64_t sent() const { return sent_; }

private:
    static void on_input(void* context, IOReturn result, void* sender, IOHIDReportType type, std::uint32_t id,
                         std::uint8_t* report, CFIndex length);
    void close();

    IOHIDDeviceRef device_ = nullptr;
    bool open_ = false;
    dispatch_queue_t queue_ = nullptr;
    dispatch_semaphore_t cancelled_ = nullptr;
    std::vector<std::uint8_t> buffer_;
    std::mutex inputs_mutex_;
    std::condition_variable input_arrived_;
    std::deque<Bytes> inputs_;
    Bytes latest_;
    std::atomic<std::uint64_t> sent_{0};
};
