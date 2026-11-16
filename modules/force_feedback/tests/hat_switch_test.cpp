// Finding and reading a wheel's hat switch in its input reports (hat_switch.h).

#include "drivers/hat_switch.h"
#include "test.h"

namespace {

// A wheel: report 1 with a hat (eight ways, 0 to 7, 8 for centred) in four
// bits, four padding bits, then eight buttons.
const Bytes kWheel = {
    0x05, 0x01, 0x09, 0x04, 0xA1, 0x01,              // Generic Desktop, Joystick, application
    0x85, 0x01,                                      // report 1
    0x09, 0x39,                                      // Hat Switch
    0x15, 0x00, 0x25, 0x07, 0x75, 0x04, 0x95, 0x01,  // 0 to 7, 4 bits
    0x81, 0x42,                                      // input, null state
    0x75, 0x04, 0x95, 0x01, 0x81, 0x03,              // 4 bits padding
    0x05, 0x09, 0x19, 0x01, 0x29, 0x08,              // buttons 1 to 8
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08,  // 8 x 1 bit
    0x81, 0x02,                                      // input
    0xC0,
};

}  // namespace

TEST(hat_found_by_generic_desktop_hat_switch) {
    const std::optional<HatSwitch> hat = hat_switch(ReportDescriptor::parse(kWheel));
    CHECK(hat.has_value());
    CHECK_EQ(int(hat->report_id), 1);
    CHECK_EQ(hat->bit_offset, 8u);  // after the ID byte
    CHECK_EQ(hat->size, 4u);
}

TEST(hat_read_as_godots_hat_mask) {
    const HatSwitch hat = *hat_switch(ReportDescriptor::parse(kWheel));
    CHECK_EQ(*read_hat(hat, {0x01, 0x00, 0x00}), 1);       // up
    CHECK_EQ(*read_hat(hat, {0x01, 0x02, 0x00}), 2);       // right
    CHECK_EQ(*read_hat(hat, {0x01, 0x03, 0x00}), 2 | 4);   // down and right
    CHECK_EQ(*read_hat(hat, {0x01, 0x04, 0xFF}), 4);       // down, whatever the buttons
    CHECK_EQ(*read_hat(hat, {0x01, 0x06, 0x00}), 8);       // left
    CHECK_EQ(*read_hat(hat, {0x01, 0x08, 0x00}), 0);       // centred: its null state
    CHECK(!read_hat(hat, {0x02, 0x00, 0x00}).has_value());  // another report
}

TEST(hat_none_without_one) {
    const Bytes buttons_only = {0x05, 0x01, 0x09, 0x04, 0xA1, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x08,
                                0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0xC0};
    CHECK(!hat_switch(ReportDescriptor::parse(buttons_only)).has_value());
}
