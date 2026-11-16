#include "hid_device.h"

#include "../wheel_log.h"

#include <IOKit/hid/IOHIDKeys.h>

#include <algorithm>
#include <string>

namespace {

// Input reports kept for a reader; older ones go first.
constexpr std::size_t kQueuedInputs = 256;

constexpr std::uint32_t kGenericDesktop = 0x01;
constexpr std::uint32_t kJoystick = 0x04;
constexpr std::uint32_t kGamePad = 0x05;
constexpr std::uint32_t kMultiAxisController = 0x08;

long number(IOHIDDeviceRef device, CFStringRef key) {
    const CFTypeRef value = IOHIDDeviceGetProperty(device, key);
    long out = 0;
    if (value && CFGetTypeID(value) == CFNumberGetTypeID()) {
        CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberLongType, &out);
    }
    return out;
}

std::string text(IOHIDDeviceRef device, CFStringRef key) {
    const CFTypeRef value = IOHIDDeviceGetProperty(device, key);
    if (!value || CFGetTypeID(value) != CFStringGetTypeID()) {
        return "";
    }
    char buffer[256];
    if (!CFStringGetCString(static_cast<CFStringRef>(value), buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
        return "";
    }
    return buffer;
}

Bytes descriptor(IOHIDDeviceRef device) {
    const CFTypeRef value = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDReportDescriptorKey));
    if (!value || CFGetTypeID(value) != CFDataGetTypeID()) {
        return {};
    }
    const auto data = static_cast<CFDataRef>(value);
    const std::uint8_t* bytes = CFDataGetBytePtr(data);
    return Bytes(bytes, bytes + CFDataGetLength(data));
}

void add_number(CFMutableDictionaryRef dictionary, CFStringRef key, std::uint32_t value) {
    const int number = static_cast<int>(value);
    const CFNumberRef ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &number);
    CFDictionarySetValue(dictionary, key, ref);
    CFRelease(ref);
}

CFMutableDictionaryRef matching() {
    return CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                     &kCFTypeDictionaryValueCallBacks);
}

IOHIDReportType report_type(ReportType type) {
    switch (type) {
    case ReportType::input:
        return kIOHIDReportTypeInput;
    case ReportType::output:
        return kIOHIDReportTypeOutput;
    case ReportType::feature:
        return kIOHIDReportTypeFeature;
    }
    return kIOHIDReportTypeOutput;
}

}  // namespace

FoundDevice describe(IOHIDDeviceRef device) {
    FoundDevice entry;
    entry.facts.vendor = static_cast<std::uint16_t>(number(device, CFSTR(kIOHIDVendorIDKey)));
    entry.facts.product = static_cast<std::uint16_t>(number(device, CFSTR(kIOHIDProductIDKey)));
    entry.facts.version = static_cast<std::uint16_t>(number(device, CFSTR(kIOHIDVersionNumberKey)));
    entry.facts.location = static_cast<std::uint32_t>(number(device, CFSTR(kIOHIDLocationIDKey)));
    entry.facts.product_name = text(device, CFSTR(kIOHIDProductKey));
    entry.facts.max_output_report = static_cast<std::size_t>(number(device, CFSTR(kIOHIDMaxOutputReportSizeKey)));
    entry.descriptor = descriptor(device);
    entry.facts.descriptor = ReportDescriptor::parse(entry.descriptor);
    entry.max_input_report = static_cast<std::size_t>(number(device, CFSTR(kIOHIDMaxInputReportSizeKey)));
    entry.max_feature_report = static_cast<std::size_t>(number(device, CFSTR(kIOHIDMaxFeatureReportSizeKey)));
    entry.service = IOHIDDeviceGetService(device);
    return entry;
}

HidManager::HidManager(bool controllers, std::function<void(FoundDevice&)> arrived) : arrived_(std::move(arrived)) {
    manager_ = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDManagerOptionNone);
    if (!manager_) {
        wheel_log("IOHIDManagerCreate failed");
        return;
    }
    CFMutableArrayRef criteria = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
    for (const auto& [vendor, product] : wheel_registry::products()) {
        CFMutableDictionaryRef criterion = matching();
        add_number(criterion, CFSTR(kIOHIDVendorIDKey), vendor);
        add_number(criterion, CFSTR(kIOHIDProductIDKey), product);
        CFArrayAppendValue(criteria, criterion);
        CFRelease(criterion);
    }
    for (const std::uint32_t usage : {kJoystick, kGamePad, kMultiAxisController}) {
        if (!controllers) {
            break;
        }
        CFMutableDictionaryRef criterion = matching();
        add_number(criterion, CFSTR(kIOHIDDeviceUsagePageKey), kGenericDesktop);
        add_number(criterion, CFSTR(kIOHIDDeviceUsageKey), usage);
        CFArrayAppendValue(criteria, criterion);
        CFRelease(criterion);
    }
    IOHIDManagerSetDeviceMatchingMultiple(manager_, criteria);
    CFRelease(criteria);
    if (arrived_) {
        IOHIDManagerRegisterDeviceMatchingCallback(manager_, &HidManager::on_matched, this);
    }
    // On the main run loop, which keeps its set of devices current as wheels come and go.
    IOHIDManagerScheduleWithRunLoop(manager_, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
    const IOReturn result = IOHIDManagerOpen(manager_, kIOHIDOptionsTypeNone);
    if (result != kIOReturnSuccess) {
        wheel_log("IOHIDManagerOpen failed (" + std::to_string(result) + ")");
    }
}

void HidManager::on_matched(void* context, IOReturn result, void*, IOHIDDeviceRef device) {
    auto* self = static_cast<HidManager*>(context);
    if (result != kIOReturnSuccess || !device || !self->arrived_) {
        return;
    }
    FoundDevice found = describe(device);
    self->arrived_(found);
}

HidManager::~HidManager() {
    if (manager_) {
        IOHIDManagerUnscheduleFromRunLoop(manager_, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
        IOHIDManagerClose(manager_, kIOHIDOptionsTypeNone);
        CFRelease(manager_);
    }
}

std::vector<FoundDevice> HidManager::devices() {
    std::vector<FoundDevice> found;
    if (!manager_) {
        return found;
    }
    const CFSetRef set = IOHIDManagerCopyDevices(manager_);
    if (!set) {
        return found;
    }
    const CFIndex count = CFSetGetCount(set);
    std::vector<const void*> refs(static_cast<std::size_t>(count));
    CFSetGetValues(set, refs.data());
    for (const void* ref : refs) {
        found.push_back(describe(static_cast<IOHIDDeviceRef>(const_cast<void*>(ref))));
    }
    CFRelease(set);
    // A stable order (IOKit's set has none), so the same wheel is chosen each time.
    std::sort(found.begin(), found.end(), [](const FoundDevice& a, const FoundDevice& b) {
        return a.service < b.service;
    });
    return found;
}

// A device object of our own for the service, so that cancelling its queue
// on close leaves the manager's untouched.
HidDevice::HidDevice(io_service_t service) {
    if (service != IO_OBJECT_NULL) {
        device_ = IOHIDDeviceCreate(kCFAllocatorDefault, service);
    }
}

HidDevice::~HidDevice() {
    close();
    if (device_) {
        CFRelease(device_);
    }
}

bool HidDevice::open(bool listen, std::size_t max_input_report) {
    if (!device_) {
        return false;
    }
    const IOReturn result = IOHIDDeviceOpen(device_, kIOHIDOptionsTypeNone);
    if (result != kIOReturnSuccess) {
        wheel_log("IOHIDDeviceOpen failed (" + std::to_string(result) + ")");
        return false;
    }
    open_ = true;
    if (listen && max_input_report > 0) {
        buffer_.assign(max_input_report, 0);
        queue_ = dispatch_queue_create("force_feedback.hid", DISPATCH_QUEUE_SERIAL);
        cancelled_ = dispatch_semaphore_create(0);
        dispatch_semaphore_t cancelled = cancelled_;
        IOHIDDeviceRegisterInputReportCallback(device_, buffer_.data(), static_cast<CFIndex>(buffer_.size()),
                                               &HidDevice::on_input, this);
        IOHIDDeviceSetDispatchQueue(device_, queue_);
        IOHIDDeviceSetCancelHandler(device_, ^{
          dispatch_semaphore_signal(cancelled);
        });
        IOHIDDeviceActivate(device_);
    }
    return true;
}

void HidDevice::close() {
    if (open_) {
        IOHIDDeviceClose(device_, kIOHIDOptionsTypeNone);
        open_ = false;
    }
    if (queue_) {
        // No input callback runs once the cancel handler has.
        IOHIDDeviceCancel(device_);
        dispatch_semaphore_wait(cancelled_, DISPATCH_TIME_FOREVER);
        dispatch_release(cancelled_);
        dispatch_release(queue_);
        queue_ = nullptr;
        cancelled_ = nullptr;
    }
}

void HidDevice::on_input(void* context, IOReturn result, void*, IOHIDReportType, std::uint32_t, std::uint8_t* report,
                         CFIndex length) {
    auto* self = static_cast<HidDevice*>(context);
    if (result != kIOReturnSuccess || length <= 0) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(self->inputs_mutex_);
        if (self->inputs_.size() >= kQueuedInputs) {
            self->inputs_.pop_front();
        }
        self->inputs_.emplace_back(report, report + length);
        self->latest_.assign(report, report + length);
    }
    self->input_arrived_.notify_all();
}

std::optional<Bytes> HidDevice::latest_input() {
    std::lock_guard<std::mutex> lock(inputs_mutex_);
    if (latest_.empty()) {
        return std::nullopt;
    }
    return latest_;
}

bool HidDevice::send(ReportType type, std::uint8_t id, const Bytes& report) {
    if (!open_ || report.empty()) {
        return false;
    }
    ++sent_;
    return IOHIDDeviceSetReport(device_, report_type(type), id, report.data(), static_cast<CFIndex>(report.size())) ==
           kIOReturnSuccess;
}

std::optional<Bytes> HidDevice::read_feature(std::uint8_t id, std::size_t length) {
    if (!open_ || length == 0) {
        return std::nullopt;
    }
    Bytes report(length, 0);
    CFIndex got = static_cast<CFIndex>(length);
    if (IOHIDDeviceGetReport(device_, kIOHIDReportTypeFeature, id, report.data(), &got) != kIOReturnSuccess) {
        return std::nullopt;
    }
    report.resize(static_cast<std::size_t>(std::max<CFIndex>(got, 0)));
    return report;
}

std::optional<Bytes> HidDevice::next_input(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(inputs_mutex_);
    if (!input_arrived_.wait_for(lock, timeout, [this] { return !inputs_.empty(); })) {
        return std::nullopt;
    }
    Bytes report = std::move(inputs_.front());
    inputs_.pop_front();
    return report;
}
