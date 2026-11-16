// Fanatec: gotzl/hid-fanatecff's hid-ftecff.c.

#include "drivers/fanatec/fanatec.h"
#include "recording_transport.h"
#include "test.h"

TEST(fanatec_open) {
    RecordingTransport base;
    FanatecDriver driver(base, 0x01, 8, true, 1090);
    CHECK(driver.open());
    CHECK_EQ(base.reports(), (std::vector<Bytes>{
                                 // ftecff_init's common initialisation.
                                 {0x01, 0xF8, 0x09, 0x01, 0x06, 0xFF, 0x01, 0x00},
                                 {0x01, 0xF8, 0x09, 0x01, 0x06, 0xFF, 0x00, 0x00},
                                 {0x01, 0xF8, 0x09, 0x01, 0x06, 0xFF, 0x04, 0x00},
                                 // ftec_set_range to the base's most (1090, "auto"): its centring off.
                                 {0x01, 0xF5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                                 {0x01, 0xF8, 0x09, 0x01, 0x06, 0x01, 0x00, 0x00},
                                 {0x01, 0xF8, 0x81, 0x42, 0x04, 0x00, 0x00, 0x00},
                                 // ftecff_init_slots: every slot stopped.
                                 {0x01, 0x03, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00},
                                 {0x01, 0x13, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00},
                                 {0x01, 0x23, 0x0C, 0x00, 0x00, 0x00, 0x00, 0xFF},
                                 {0x01, 0x33, 0x0C, 0x00, 0x00, 0x00, 0x00, 0xFF},
                                 {0x01, 0x43, 0x0C, 0x00, 0x00, 0x00, 0x00, 0xFF},
                             }));
    CHECK_EQ(int(base.sent[0].id), 1);
}

TEST(fanatec_force_each_way) {
    // TRANSLATE_FORCE puts no force at 0x8000 and a positive Linux level
    // above it (direction 0x4000 for a wheel, so a positive DirectInput
    // force stays positive), the same as a Logitech classic wheel, where
    // above 0x80 turns the wheel to the left; hid-fanatecff takes its
    // effects from new-lg4ff.
    RecordingTransport high;
    FanatecDriver dd(high, 0x01, 8, true, 2530);
    dd.open();
    std::size_t from = high.sent.size();
    CHECK(dd.set_force(1.0));
    CHECK(dd.set_force(-1.0));
    CHECK(dd.set_force(0.0));
    CHECK_EQ(high.reports(from), (std::vector<Bytes>{
                                     {0x01, 0x01, 0x08, 0xFF, 0xFF, 0x00, 0x00, 0x01},
                                     {0x01, 0x01, 0x08, 0x01, 0x00, 0x00, 0x00, 0x01},
                                     {0x01, 0x03, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00},
                                 }));

    RecordingTransport low;
    FanatecDriver v2(low, 0x01, 8, false, 900);
    v2.open();
    from = low.sent.size();
    CHECK(v2.set_force(1.0));
    CHECK(v2.set_force(0.999));  // the same 8-bit step: nothing new
    CHECK(v2.set_force(-1.0));
    CHECK_EQ(low.reports(from), (std::vector<Bytes>{
                                    {0x01, 0x01, 0x08, 0xFF, 0x00, 0x00, 0x00, 0x00},
                                    {0x01, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00},
                                }));
}

TEST(fanatec_damper) {
    // Slot 2, command 0x0C, both ways, the bytes between 0, clip full: 8
    // bits on a high resolution base (enable_highres_dynamic_effects), 4
    // (SCALE_COEFF's default) on the rest.
    RecordingTransport high;
    FanatecDriver dd(high, 0x01, 8, true, 1090);
    dd.open();
    std::size_t from = high.sent.size();
    CHECK(dd.set_damper(1.0));
    CHECK(dd.set_damper(0.999));  // the same eight bits
    CHECK(dd.set_damper(0.25));
    CHECK(dd.set_damper(0.0));
    CHECK_EQ(high.reports(from), (std::vector<Bytes>{
                                     {0x01, 0x21, 0x0C, 0xFF, 0x00, 0xFF, 0x00, 0xFF},
                                     {0x01, 0x21, 0x0C, 0x40, 0x00, 0x40, 0x00, 0xFF},
                                     {0x01, 0x23, 0x0C, 0x00, 0x00, 0x00, 0x00, 0xFF},
                                 }));
    RecordingTransport low;
    FanatecDriver v2(low, 0x01, 8, false, 900);
    v2.open();
    from = low.sent.size();
    CHECK(v2.set_damper(1.0));
    CHECK(v2.set_damper(0.99));  // the same four bits
    CHECK_EQ(low.reports(from), (std::vector<Bytes>{{0x01, 0x21, 0x0C, 0x0F, 0x00, 0x0F, 0x00, 0xFF}}));
}

TEST(fanatec_friction) {
    // ftecff_init_slots: friction is slot 4, command 0x0C, as the damper.
    RecordingTransport base;
    FanatecDriver driver(base, 0x01, 8, true, 2530);
    driver.open();
    const std::size_t from = base.sent.size();
    CHECK(driver.set_friction(0.5));
    CHECK(driver.set_friction(0.0));
    CHECK_EQ(base.reports(from), (std::vector<Bytes>{
                                     {0x01, 0x41, 0x0C, 0x80, 0x00, 0x80, 0x00, 0xFF},
                                     {0x01, 0x43, 0x0C, 0x00, 0x00, 0x00, 0x00, 0xFF},
                                 }));
    CHECK_EQ(driver.max_update_hz(), 500);  // hid-ftecff's 2 ms timer
}

TEST(fanatec_rotation) {
    // ftec_set_range, from 90 degrees to the base's most (2530 on a CSL DD,
    // its "auto"), and back to its most once let go.
    RecordingTransport base;
    FanatecDriver driver(base, 0x01, 8, true, 2530);
    driver.open();
    CHECK_EQ(driver.rotation(), 2530);
    std::size_t from = base.sent.size();
    CHECK_EQ(driver.set_rotation(900), 900);
    CHECK_EQ(driver.set_rotation(10), 90);
    CHECK_EQ(driver.set_rotation(3000), 2530);
    CHECK_EQ(base.reports(from), (std::vector<Bytes>{
                                     {0x01, 0xF5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                                     {0x01, 0xF8, 0x09, 0x01, 0x06, 0x01, 0x00, 0x00},
                                     {0x01, 0xF8, 0x81, 0x84, 0x03, 0x00, 0x00, 0x00},
                                     {0x01, 0xF5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                                     {0x01, 0xF8, 0x09, 0x01, 0x06, 0x01, 0x00, 0x00},
                                     {0x01, 0xF8, 0x81, 0x5A, 0x00, 0x00, 0x00, 0x00},
                                     {0x01, 0xF5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                                     {0x01, 0xF8, 0x09, 0x01, 0x06, 0x01, 0x00, 0x00},
                                     {0x01, 0xF8, 0x81, 0xE2, 0x09, 0x00, 0x00, 0x00},
                                 }));
    driver.set_rotation(1080);
    from = base.sent.size();
    driver.close();
    const std::vector<Bytes> closing = base.reports(from);
    CHECK_EQ(closing.back(), (Bytes{0x01, 0xF8, 0x81, 0xE2, 0x09, 0x00, 0x00, 0x00}));
}

TEST(fanatec_too_short_a_report_isnt_opened) {
    RecordingTransport base;
    FanatecDriver driver(base, 0x01, 4, true, 1090);
    CHECK(!driver.open());
    CHECK(base.sent.empty());
}
