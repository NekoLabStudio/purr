#include "usb_device.h"

#include "../wheel_log.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>

#include <cstdio>
#include <string>

namespace usb_device {

namespace {

constexpr UInt32 kTimeoutMs = 1000;

// The first USB device with these IDs, as a service; IO_OBJECT_NULL if none.
io_service_t first_service(std::uint16_t vendor, std::uint16_t product) {
    CFMutableDictionaryRef wanted = matching(vendor, product);
    if (!wanted) {
        return IO_OBJECT_NULL;
    }
    io_iterator_t services = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(MACH_PORT_NULL,  // the default port, on every macOS
                                     wanted, &services) != KERN_SUCCESS) {
        return IO_OBJECT_NULL;
    }
    const io_service_t service = IOIteratorNext(services);
    IOObjectRelease(services);
    return service;
}

// The first USB device with these IDs, as a device interface, unopened.
IOUSBDeviceInterface500** find(std::uint16_t vendor, std::uint16_t product) {
    const io_service_t service = first_service(vendor, product);
    if (service == IO_OBJECT_NULL) {
        return nullptr;
    }
    IOCFPlugInInterface** plugin = nullptr;
    SInt32 score = 0;
    const kern_return_t made =
        IOCreatePlugInInterfaceForService(service, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
    IOObjectRelease(service);
    if (made != KERN_SUCCESS || !plugin) {
        return nullptr;
    }
    IOUSBDeviceInterface500** device = nullptr;
    if ((*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID500),
                                  reinterpret_cast<LPVOID*>(&device)) != S_OK) {
        device = nullptr;
    }
    IODestroyPlugInInterface(plugin);
    return device;
}

// The wheel leaves the bus as it takes a switch, before the transfer can
// complete: these mean it went (bootswitch.c's t150_usb_left_the_bus).
bool went(IOReturn result) {
    return result == kIOReturnSuccess || result == kIOReturnNotResponding || result == kIOUSBPipeStalled ||
           result == kIOReturnNoDevice || result == kIOReturnAborted;
}

std::string code(IOReturn result) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08X", static_cast<unsigned>(result));
    return text;
}

// One interface's user client, unopened.
IOUSBInterfaceInterface500** interface_of(io_service_t service) {
    IOCFPlugInInterface** plugin = nullptr;
    SInt32 score = 0;
    const kern_return_t made = IOCreatePlugInInterfaceForService(service, kIOUSBInterfaceUserClientTypeID,
                                                                 kIOCFPlugInInterfaceID, &plugin, &score);
    if (made != KERN_SUCCESS || !plugin) {
        return nullptr;
    }
    IOUSBInterfaceInterface500** interface = nullptr;
    if ((*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID500),
                                  reinterpret_cast<LPVOID*>(&interface)) != S_OK) {
        interface = nullptr;
    }
    IODestroyPlugInInterface(plugin);
    return interface;
}

// Writes `message` on the interface's OUT endpoint `endpoint`, if it has
// one: nothing if it hasn't, else whether it went.
std::optional<bool> write_on(IOUSBInterfaceInterface500** interface, std::uint8_t endpoint, const Bytes& message,
                             const std::string& id) {
    const IOReturn opened = (*interface)->USBInterfaceOpen(interface);
    if (opened != kIOReturnSuccess) {
        wheel_log(id + ": its USB interface couldn't be opened (" + code(opened) +
                  "), so it can't be switched to its own mode; is another driver using it?");
        return std::nullopt;
    }
    std::optional<bool> sent;
    UInt8 pipes = 0;
    (*interface)->GetNumEndpoints(interface, &pipes);
    for (UInt8 pipe = 1; pipe <= pipes && !sent; ++pipe) {
        UInt8 direction = 0, number = 0, transfer = 0, interval = 0;
        UInt16 size = 0;
        if ((*interface)->GetPipeProperties(interface, pipe, &direction, &number, &transfer, &size, &interval) !=
                kIOReturnSuccess ||
            direction != kUSBOut || number != endpoint) {
            continue;
        }
        Bytes copy = message;
        sent = went((*interface)->WritePipeTO(interface, pipe, copy.data(), static_cast<UInt32>(copy.size()),
                                              kTimeoutMs, kTimeoutMs));
    }
    (*interface)->USBInterfaceClose(interface);
    return sent;
}

}  // namespace

CFMutableDictionaryRef matching(std::uint16_t vendor, std::uint16_t product) {
    CFMutableDictionaryRef wanted = IOServiceMatching(kIOUSBDeviceClassName);
    if (!wanted) {
        return nullptr;
    }
    const SInt32 vendor_id = vendor;
    const SInt32 product_id = product;
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &vendor_id);
    CFDictionarySetValue(wanted, CFSTR(kUSBVendorID), number);
    CFRelease(number);
    number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &product_id);
    CFDictionarySetValue(wanted, CFSTR(kUSBProductID), number);
    CFRelease(number);
    return wanted;
}

bool attached(std::uint16_t vendor, std::uint16_t product) {
    const io_service_t service = first_service(vendor, product);
    if (service == IO_OBJECT_NULL) {
        return false;
    }
    IOObjectRelease(service);
    return true;
}

std::pair<std::uint16_t, std::uint16_t> ids(io_service_t service) {
    const auto read = [service](CFStringRef key) {
        long value = 0;
        const CFTypeRef property = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
        if (property) {
            if (CFGetTypeID(property) == CFNumberGetTypeID()) {
                CFNumberGetValue(static_cast<CFNumberRef>(property), kCFNumberLongType, &value);
            }
            CFRelease(property);
        }
        return static_cast<std::uint16_t>(value);
    };
    return {read(CFSTR(kUSBVendorID)), read(CFSTR(kUSBProductID))};
}

std::optional<Bytes> request_in(std::uint16_t vendor, std::uint16_t product, std::uint8_t request_type,
                                std::uint8_t request, std::uint16_t length) {
    IOUSBDeviceInterface500** device = find(vendor, product);
    if (!device) {
        return std::nullopt;
    }
    Bytes answer(length, 0);
    IOUSBDevRequestTO call{};
    call.bmRequestType = request_type;
    call.bRequest = request;
    call.wLength = length;
    call.pData = answer.data();
    call.noDataTimeout = kTimeoutMs;
    call.completionTimeout = kTimeoutMs;
    const IOReturn result = (*device)->DeviceRequestTO(device, &call);
    (*device)->Release(device);
    if (result != kIOReturnSuccess) {
        return std::nullopt;
    }
    answer.resize(call.wLenDone);
    return answer;
}

bool request_out(std::uint16_t vendor, std::uint16_t product, std::uint8_t request_type, std::uint8_t request,
                 std::uint16_t value) {
    IOUSBDeviceInterface500** device = find(vendor, product);
    if (!device) {
        return false;
    }
    IOUSBDevRequestTO call{};
    call.bmRequestType = request_type;
    call.bRequest = request;
    call.wValue = value;
    call.noDataTimeout = kTimeoutMs;
    call.completionTimeout = kTimeoutMs;
    const IOReturn result = (*device)->DeviceRequestTO(device, &call);
    (*device)->Release(device);
    return went(result);
}

bool write_endpoint(std::uint16_t vendor, std::uint16_t product, std::uint8_t endpoint, const Bytes& message) {
    const std::string id = usb_id(vendor, product);
    IOUSBDeviceInterface500** device = find(vendor, product);
    if (!device) {
        wheel_log(id + ": its USB device couldn't be reached");
        return false;
    }
    // A vendor class device no driver has taken may be unconfigured, with no
    // interfaces yet: configure it, as usb_modeswitch's target expects.
    UInt8 configuration = 0;
    (*device)->GetConfiguration(device, &configuration);
    if (configuration == 0) {
        IOUSBConfigurationDescriptorPtr first = nullptr;
        const bool opened = (*device)->USBDeviceOpen(device) == kIOReturnSuccess;
        if (opened && (*device)->GetConfigurationDescriptorPtr(device, 0, &first) == kIOReturnSuccess && first) {
            (*device)->SetConfiguration(device, first->bConfigurationValue);
        }
        if (opened) {
            (*device)->USBDeviceClose(device);
        }
    }
    IOUSBFindInterfaceRequest any{kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare,
                                  kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare};
    io_iterator_t interfaces = IO_OBJECT_NULL;
    std::optional<bool> sent;
    if ((*device)->CreateInterfaceIterator(device, &any, &interfaces) == kIOReturnSuccess) {
        while (const io_service_t service = IOIteratorNext(interfaces)) {
            IOUSBInterfaceInterface500** interface = sent ? nullptr : interface_of(service);
            IOObjectRelease(service);
            if (interface) {
                sent = write_on(interface, endpoint, message, id);
                (*interface)->Release(interface);
            }
        }
        IOObjectRelease(interfaces);
    }
    (*device)->Release(device);
    if (!sent) {
        wheel_log(id + ": no USB interface with OUT endpoint " + std::to_string(endpoint) + " to switch it with");
    }
    return sent.value_or(false);
}

}  // namespace usb_device
