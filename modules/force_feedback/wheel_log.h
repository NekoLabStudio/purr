#pragma once

// One line about the wheel for the game's log: the driver a wheel opened
// with, and anything that failed. Never per update, and never the same line
// twice. The extension sends the lines through Godot (register_types.cpp);
// the tests catch them.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

void wheel_log(const std::string& line);
void set_wheel_log(std::function<void(const std::string&)> sink);

// "046D:C24F", as the logs name a device, and one such number alone.
std::string usb_id(std::uint16_t vendor, std::uint16_t product);
std::string hex4(std::uint16_t number);
// Bytes as hex digits, two a byte, nothing between ("0501A1"): a report
// descriptor a tester's log carries, ready to be a test fixture.
std::string hex_bytes(const std::vector<std::uint8_t>& bytes);
