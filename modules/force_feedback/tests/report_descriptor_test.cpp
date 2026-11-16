#include "descriptors/openffboard.h"
#include "drivers/report_descriptor.h"
#include "test.h"

namespace {

// A joystick with one input axis, a vendor output pair and a PID-style
// array of effect types inside a logical collection.
const Bytes kSmall = {
    0x05, 0x01, 0x09, 0x04, 0xA1, 0x01,                    // Generic Desktop, Joystick, application
    0x85, 0x03,                                            // report 3
    0x09, 0x30, 0x15, 0x00, 0x26, 0xFF, 0x03,              // X, 0 to 1023
    0x75, 0x10, 0x95, 0x01, 0x81, 0x02,                    // 16 bits, input
    0x06, 0x00, 0xFF, 0x09, 0x01, 0x15, 0x80, 0x25, 0x7F,  // vendor usage 1, -128 to 127
    0x55, 0x0E,                                            // unit exponent -2
    0x75, 0x08, 0x95, 0x02, 0x91, 0x02,                    // 2 x 8 bits, output variable
    0x05, 0x0F, 0x09, 0x25, 0xA1, 0x02,                    // PID, Effect Type, logical collection
    0x09, 0x26, 0x09, 0x41, 0x15, 0x01, 0x25, 0x02,        // ET Constant, ET Damper, 1 to 2
    0x75, 0x08, 0x95, 0x01, 0x91, 0x00,                    // 8 bits, output array
    0xC0, 0xC0,
};

}  // namespace

TEST(descriptor_fields) {
    const ReportDescriptor descriptor = ReportDescriptor::parse(kSmall);
    CHECK(descriptor.numbered());
    CHECK_EQ(descriptor.fields().size(), std::size_t{3});
    const HidField& axis = descriptor.fields()[0];
    CHECK(axis.type == ReportType::input);
    CHECK_EQ(int(axis.report_id), 3);
    CHECK_EQ(axis.bit_offset, 8u);
    CHECK_EQ(axis.logical_max, 1023);
    const HidField& pair = descriptor.fields()[1];
    CHECK(pair.type == ReportType::output);
    CHECK_EQ(pair.bit_offset, 8u);
    CHECK_EQ(pair.logical_min, -128);
    CHECK_EQ(pair.logical_max, 127);
    CHECK_EQ(pair.unit_exponent, -2);
    CHECK_EQ(pair.usages.size(), std::size_t{2});
    CHECK_EQ(pair.slot_of(0xFF000001), 0);
    const HidField& types = descriptor.fields()[2];
    CHECK(!types.variable);
    CHECK_EQ(types.bit_offset, 24u);
    CHECK_EQ(types.logical, 0x000F0025u);
    CHECK_EQ(types.key_of(0x000F0041), 2);
    CHECK_EQ(types.slot_of(0x000F0041), -1);
    CHECK_EQ(descriptor.length(ReportType::output, 3), std::size_t{4});
    CHECK_EQ(descriptor.length(ReportType::feature, 3), std::size_t{0});
    CHECK(descriptor.uses_page(0x0F));
    CHECK(!descriptor.uses_page(0x09));
}

TEST(descriptor_malformed_is_empty) {
    Bytes cut = kSmall;
    cut.pop_back();  // an unclosed collection
    CHECK(ReportDescriptor::parse(cut).fields().empty());
    CHECK(ReportDescriptor::parse({0x26, 0xFF}).fields().empty());  // an item cut short
}

TEST(descriptor_openffboard) {
    const ReportDescriptor descriptor =
        ReportDescriptor::parse(Bytes(std::begin(kOpenFfboardDescriptor), std::end(kOpenFfboardDescriptor)));
    CHECK(descriptor.numbered());
    CHECK(descriptor.uses_page(0x0F));
    CHECK(!descriptor.report_ids(ReportType::output).empty());
    CHECK(!descriptor.report_ids(ReportType::feature).empty());
}

TEST(bits_round_trip) {
    Bytes report(4, 0x00);
    put_bits(report, 4, 12, -2);
    CHECK_EQ(report, (Bytes{0xE0, 0xFF, 0x00, 0x00}));
    CHECK_EQ(get_bits(report, 4, 12, true), std::int64_t{-2});
    CHECK_EQ(get_bits(report, 4, 12, false), std::int64_t{0xFFE});
    put_bits(report, 16, 16, 0x1234);
    CHECK_EQ(report, (Bytes{0xE0, 0xFF, 0x34, 0x12}));
}
