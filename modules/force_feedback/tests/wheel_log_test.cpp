// The log's lines: each once, and a new PID wheel's descriptor dumped as hex
// a tester's log can carry back as a fixture (tests/descriptors).

#include "test.h"
#include "wheel_log.h"

TEST(log_says_each_line_once) {
    std::vector<std::string> lines;
    set_wheel_log([&lines](const std::string& line) { lines.push_back(line); });
    const std::vector<std::uint8_t> descriptor = {0x05, 0x01, 0x09, 0x04, 0xA1, 0x01, 0xC0};
    wheel_log("Moza R9 (346E:0002) report descriptor: " + hex_bytes(descriptor));
    wheel_log("Moza R9 (346E:0002) report descriptor: " + hex_bytes(descriptor));  // the game retrying
    CHECK_EQ(lines.size(), std::size_t{1});
    CHECK_EQ(lines[0], std::string("Force feedback: Moza R9 (346E:0002) report descriptor: 05010904A101C0"));
    set_wheel_log([](const std::string&) {});
}
