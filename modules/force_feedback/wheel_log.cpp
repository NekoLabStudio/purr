#include "wheel_log.h"

#include <cstdio>
#include <mutex>
#include <set>

namespace {

std::mutex sink_mutex;
std::function<void(const std::string&)> sink;
// The game retries a wheel every few seconds; each line is logged once.
std::set<std::string> logged;

}  // namespace

void wheel_log(const std::string& line) {
    std::lock_guard<std::mutex> lock(sink_mutex);
    if (!logged.insert(line).second) {
        return;
    }
    if (sink) {
        sink("Force feedback: " + line);
    } else {
        std::fprintf(stderr, "Force feedback: %s\n", line.c_str());
    }
}

void set_wheel_log(std::function<void(const std::string&)> new_sink) {
    std::lock_guard<std::mutex> lock(sink_mutex);
    sink = std::move(new_sink);
    logged.clear();
}

std::string hex4(std::uint16_t number) {
    char text[8];
    std::snprintf(text, sizeof(text), "%04X", number);
    return text;
}

std::string usb_id(std::uint16_t vendor, std::uint16_t product) {
    return hex4(vendor) + ":" + hex4(product);
}

std::string hex_bytes(const std::vector<std::uint8_t>& bytes) {
    static const char digits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::uint8_t byte : bytes) {
        out += digits[byte >> 4];
        out += digits[byte & 0x0F];
    }
    return out;
}
