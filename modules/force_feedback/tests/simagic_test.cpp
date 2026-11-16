// Simagic's own reports, from JacKeTUs/simagic-ff's hid-simagic.c
// (sm_set_effect_report, sm_set_constant_report, sm_set_condition_report,
// sm_req_playback, sm_set_gain).

#include "drivers/simagic/simagic.h"
#include "recording_transport.h"
#include "test.h"

TEST(simagic_open) {
    // Each packet in the base's first output report (here unnumbered, 64
    // bytes): the gain full, then each effect set up endless in its fixed
    // block, given nothing, and started once.
    RecordingTransport base;
    SimagicDriver driver(base, 0, 64);
    CHECK(driver.open());
    const std::vector<Bytes> sent = base.reports();
    CHECK_EQ(sent.size(), std::size_t{10});
    for (const Sent& one : base.sent) {
        CHECK(one.type == ReportType::output);
        CHECK_EQ(int(one.id), 0);
        CHECK_EQ(one.report.size(), std::size_t{64});
    }
    CHECK_EQ(sent[0], padded({0x40, 0xFF}, 64));
    // Set Effect: block 1, 1, duration 0xFFFF, four zeros, gain 0xFF, trigger 0xFF, 04 3F.
    CHECK_EQ(sent[1], padded({0x01, 0x01, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x04, 0x3F}, 64));
    CHECK_EQ(sent[2], padded({0x05, 0x01, 0x00, 0x00}, 64));        // Set Constant: nothing
    CHECK_EQ(sent[3], padded({0x0A, 0x01, 0x01, 0x01, 0x00}, 64));  // started, once
    // The damper in block 5: centre 0, both coefficients 0, saturations 10000, dead band 0.
    CHECK_EQ(sent[4], padded({0x01, 0x05, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x04, 0x3F}, 64));
    CHECK_EQ(sent[5], padded({0x03, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x10, 0x27, 0x00,
                              0x00},
                             64));
    CHECK_EQ(sent[6], padded({0x0A, 0x05, 0x01, 0x01, 0x00}, 64));
    // Friction in block 7.
    CHECK_EQ(sent[7][1], std::uint8_t{0x07});
    CHECK_EQ(sent[8][1], std::uint8_t{0x07});
    CHECK_EQ(sent[9], padded({0x0A, 0x07, 0x01, 0x01, 0x00}, 64));
}

TEST(simagic_force_each_way) {
    // The magnitude from -10000 to 10000 (sm_rescale_signed_to_10k), taken
    // straight from a Linux level: positive to the left, as the PID wheels
    // (a tester to confirm).
    RecordingTransport base;
    SimagicDriver driver(base, 0, 64);
    driver.open();
    const std::size_t from = base.sent.size();
    CHECK(driver.set_force(1.0));
    CHECK(driver.set_force(1.0));
    CHECK(driver.set_force(-0.5));
    CHECK_EQ(base.reports(from), (std::vector<Bytes>{
                                     padded({0x05, 0x01, 0x10, 0x27}, 64),
                                     padded({0x05, 0x01, 0x78, 0xEC}, 64),
                                 }));
}

TEST(simagic_damper_and_friction) {
    RecordingTransport base;
    SimagicDriver driver(base, 0x01, 65);
    driver.open();
    const std::size_t from = base.sent.size();
    CHECK(driver.set_damper(0.5));
    CHECK(driver.set_friction(1.0));
    // Numbered: the report's ID, then the packet.
    CHECK_EQ(base.reports(from), (std::vector<Bytes>{
                                     padded({0x01, 0x03, 0x05, 0x00, 0x00, 0x00, 0x88, 0x13, 0x88, 0x13, 0x10, 0x27,
                                             0x10, 0x27, 0x00, 0x00},
                                            65),
                                     padded({0x01, 0x03, 0x07, 0x00, 0x00, 0x00, 0x10, 0x27, 0x10, 0x27, 0x10, 0x27,
                                             0x10, 0x27, 0x00, 0x00},
                                            65),
                                 }));
    CHECK_EQ(int(base.sent[from].id), 1);
}

TEST(simagic_close_stops_each_effect) {
    RecordingTransport base;
    SimagicDriver driver(base, 0, 64);
    driver.open();
    driver.set_force(0.25);
    const std::size_t from = base.sent.size();
    driver.close();
    CHECK_EQ(base.reports(from), (std::vector<Bytes>{
                                     padded({0x05, 0x01, 0x00, 0x00}, 64),
                                     padded({0x0A, 0x01, 0x03, 0x00, 0x00}, 64),
                                     padded({0x0A, 0x05, 0x03, 0x00, 0x00}, 64),
                                     padded({0x0A, 0x07, 0x03, 0x00, 0x00}, 64),
                                 }));
}

TEST(simagic_too_short_a_report_isnt_opened) {
    RecordingTransport base;
    SimagicDriver driver(base, 0x01, 8);
    CHECK(!driver.open());
    CHECK(base.sent.empty());
}
