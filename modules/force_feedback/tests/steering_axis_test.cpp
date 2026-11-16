// Finding and reading a wheel's steering in its input reports (steering_axis.h).

#include "drivers/steering_axis.h"
#include "test.h"

#include <cmath>

namespace {

// A wheel: report 1 with a button byte, then X 0 to 65535 in 16 bits.
const Bytes kWheel = {
    0x05, 0x01, 0x09, 0x04, 0xA1, 0x01,              // Generic Desktop, Joystick, application
    0x85, 0x01,                                      // report 1
    0x05, 0x09, 0x19, 0x01, 0x29, 0x08,              // buttons 1 to 8
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08,  // 8 x 1 bit
    0x81, 0x02,                                      // input
    0x05, 0x01, 0x09, 0x30,                          // X
    0x15, 0x00, 0x27, 0xFF, 0xFF, 0x00, 0x00,        // 0 to 65535
    0x75, 0x10, 0x95, 0x01, 0x81, 0x02,              // 16 bits, input
    0xC0,
};

}  // namespace

TEST(steering_axis_found_by_generic_desktop_x) {
    const std::optional<SteeringAxis> axis = steering_axis(ReportDescriptor::parse(kWheel));
    CHECK(axis.has_value());
    CHECK_EQ(int(axis->report_id), 1);
    CHECK_EQ(axis->bit_offset, 16u);  // after the ID byte and the buttons
    CHECK_EQ(axis->size, 16u);
}

TEST(steering_read_over_its_logical_range) {
    const SteeringAxis axis = *steering_axis(ReportDescriptor::parse(kWheel));
    CHECK(std::abs(*read_steering(axis, {0x01, 0x00, 0x00, 0x00}) + 1.0) < 1e-9);
    CHECK(std::abs(*read_steering(axis, {0x01, 0x00, 0xFF, 0xFF}) - 1.0) < 1e-9);
    CHECK(std::abs(*read_steering(axis, {0x01, 0x00, 0x00, 0x80})) < 1e-4);
    CHECK(!read_steering(axis, {0x02, 0x00, 0x00, 0x80}).has_value());  // another report
    CHECK(!read_steering(axis, {0x01, 0x00}).has_value());              // too short
}
